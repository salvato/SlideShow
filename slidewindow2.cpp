#include "slidewindow2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <iostream>

#include <QDebug>
#include <QDir>
#include <QPainter>
#include <QTime>

#include "bcm_host.h"
#include "math.h"


#define BUFFER_OFFSET(i) ((char *)NULL + i)

#define STEADY_SHOW_TIME       3000 // Change slide time
#define TRANSITION_TIME        1500 // Transition duration
#define UPDATE_TIME              20 // Time between screen updates


SlideWindow::SlideWindow()
    : QObject()
{
    qsrand(QTime::currentTime().msec());

    imageMode   = Qt::KeepAspectRatio;
    imageFormat = QImage::Format_RGBA8888_Premultiplied;
    pBaseImage  = Q_NULLPTR;

    viewingDistance  = 20.0;

    A      = A0      = QVector4D(0.0, -1.0, 0.0, 1.0);
    theta  = theta0  = M_PI_2;
    angle  = angle0  = 0.0f;
    alpha  = alpha0  = 1.0f;
    fScale = fScale0 = 1.0f;
    fRot   = fRot0   = 0.0f;

    steadyTime = STEADY_SHOW_TIME;
    updateTime = UPDATE_TIME;

    sSlideDir = QDir::homePath();// Just to set a default location
    iCurrentSlide = 0;

    bGLInitialized  = false;
    bEglInitialized = false;
    bRunning        = false;
    bSlidesPresent  = false;

    timerSteady.setSingleShot(true);
    connect(&timerUpdate, SIGNAL(timeout()),
            this, SLOT(ontimerUpdateEvent()));
    connect(&timerSteady, SIGNAL(timeout()),
            this, SLOT(onTimerSteadyEvent()));

    connect(&timerCheckInput, SIGNAL(timeout()),
            this, SLOT(onTimerCheckInput()));

}


SlideWindow::~SlideWindow() {
    deinitEgl();
    qDebug() << "slideshow closed";
}


int
SlideWindow::getCurrentSlide() {
    return iCurrentSlide;
}


void
SlideWindow::deinitEgl() {
    if(!bEglInitialized)
        return;
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    // clear screen
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(display, surface);
    glDeleteBuffers(1, &arrayBuf);
    eglDestroySurface(display, surface);
    dispman_update = vc_dispmanx_update_start(0);
    vc_dispmanx_element_remove(dispman_update, dispman_element);
    vc_dispmanx_update_submit_sync(dispman_update);
    vc_dispmanx_display_close(dispman_display);
    // Release OpenGL resources
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
    bcm_host_deinit();
    bEglInitialized = false;
    bGLInitialized  = false;
}


void
SlideWindow::initEglAttributes() {
    attribute_list.append(EGL_RED_SIZE);
    attribute_list.append(EGLint(8));
    attribute_list.append(EGL_GREEN_SIZE);
    attribute_list.append(EGLint(8));
    attribute_list.append(EGL_BLUE_SIZE);
    attribute_list.append(EGLint(8));
    attribute_list.append(EGL_ALPHA_SIZE);
    attribute_list.append(EGLint(8));
    attribute_list.append(EGL_LUMINANCE_SIZE);
    attribute_list.append(EGLint(EGL_DONT_CARE));
    attribute_list.append(EGL_SURFACE_TYPE);
    attribute_list.append(EGLint(EGL_WINDOW_BIT));
    attribute_list.append(EGLint(EGL_SAMPLES));
    attribute_list.append(EGLint(1));
    attribute_list.append(EGL_DEPTH_SIZE);
    attribute_list.append(EGLint(24));
    attribute_list.append(EGLint(EGL_NONE));
}


void
SlideWindow::setSlideDir(QString sNewDir) {
    qDebug() << "setSlideDir(" << sNewDir << ")";
    sSlideDir = sNewDir;
    updateSlideList();
}


void
SlideWindow::updateSlideList() {
    // Update slide list just in case we are updating the slide directory...
    slideList = QFileInfoList();
    QDir slideDir(sSlideDir);
    if(slideDir.exists()) {
        QStringList nameFilter = QStringList() << "*.jpg" << "*.jpeg" << "*.png";
        slideDir.setNameFilters(nameFilter);
        slideDir.setFilter(QDir::Files);
        slideList = slideDir.entryInfoList();
    }
    if(slideList.count() > 0) {
        bSlidesPresent = true;
    }
}


void
SlideWindow::startSlideShow(int iStartSlide) {
    iCurrentSlide = iStartSlide;
    initEgl();
    updateSlideList();
    if(bSlidesPresent) {
        if(!initializeGL()) {
            qDebug() << "GL not initialized: Could not start";
            deinitEgl();
            return;
        }
        initInputDevices();
        if((keyboardFd != -1) || (mouseFd != -1)) {
            timerCheckInput.start(200);
        }
        qDebug() << "SlideShow starting";
        paintGL();
    }
    timerSteady.start(steadyTime);
    bRunning = true;
}


void
SlideWindow::exitShow() {
    exit(EXIT_SUCCESS);
}


void
SlideWindow::stopSlideShow() {
    timerSteady.stop();
    timerUpdate.stop();
    deinitEgl();
    releaseInputDevices();
    bRunning = false;
    bSlidesPresent = false;
}


void
SlideWindow::hide() {
    timerSteady.stop();
    timerUpdate.stop();
    deinitEgl();
    releaseInputDevices();
    bRunning = false;
    bSlidesPresent = false;
}


bool
SlideWindow::isRunning() {
    return bRunning;
}


void
SlideWindow::releaseInputDevices() {
    if(keyboardFd != -1)
        close(keyboardFd);
    if(mouseFd != -1)
        close(mouseFd);
}


void
SlideWindow::initInputDevices() {
    mouseFd    = -1;
    keyboardFd = -1;
    int result;

    QString sFileDir = QString("/dev/input/by-id");
    QDir sDir(sFileDir);
    QList<QFileInfo> fileList;
    QStringList nameFilter;
    if(sDir.exists()) {
        nameFilter << "*event-kbd*";
        sDir.setNameFilters(nameFilter);
        sDir.setFilter(QDir::Files);
        fileList = sDir.entryInfoList();
        if(fileList.count() == 0) {
            qDebug() << "No keyboard found";
            return;
        }
    }
    else {
        qDebug() << sFileDir << "Not Found";
        return;
    }
    for(int i=0; i<fileList.count(); i++) {
        keyboardFd = open(fileList.at(i).absoluteFilePath().toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        result = ioctl(keyboardFd, EVIOCGRAB, 1);
//        qDebug() << "keyboardFd=" << keyboardFd << ((result == 0) ? "SUCCESS" : "FAILURE") << "in getting exclusive access";
        if(result)
            break;
    }
/*
    fileList.clear();
    nameFilter.clear();
    nameFilter << "*event-mouse*";
    sDir.setNameFilters(nameFilter);
    sDir.setFilter(QDir::Files);
    fileList = sDir.entryInfoList();
    if(fileList.count() == 0) {
        qDebug() << "No mouse found";
        return;
    }
    for(int i=0; i<fileList.count(); i++) {
        mouseFd = open(fileList.at(i).absoluteFilePath().toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        result = ioctl(mouseFd, EVIOCGRAB, 1);
        qDebug() << "mouseFd=" << mouseFd << ((result == 0) ? "SUCCESS" : "FAILURE") << "in getting exclusive access";
        if(result)
            break;
    }
*/
}


// Read a file and return its contents in a character string
char*
SlideWindow::ReadFile(QString url) {
    //open the file for reading
    QFile file(url);
    if(!file.open(QIODevice::ReadOnly)) {
        emit closing(QString("Unable to open file: %1").arg(url));
        return Q_NULLPTR;
    }
    // Get the file size
    long lSize=file.size();
    //lets allocate the memory for the file
    char *buffer = (char *) calloc(1, lSize+1);
    if(!buffer) {
        file.close();
        free(buffer);
        emit closing(QString("Unable to allocate memory for file buffer"));
        return Q_NULLPTR;
    }
    //lets copy the data into the buffer
    if(file.read(buffer, lSize) != lSize) {
        file.close();
        free(buffer);
        emit closing(QString("Error reading from file: %1").arg(url));
        return Q_NULLPTR;
    }
    file.close();
    return buffer;
}


void
SlideWindow::initEgl() {
    // No need to reinitialize...
    if(bEglInitialized)
        return;
    int32_t success = 0;

    bcm_host_init();
    initEglAttributes();

    // Let's find the max display size
    success = graphics_get_display_size(0,// Display number
                                        &screen_width,
                                        &screen_height);
    if(success < 0) {
        emit closing("Error in graphics_get_display_size()");
        return;
    }

    // get an EGL display connection
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if(display == EGL_NO_DISPLAY) {
        emit closing("Error in eglGetDisplay()");
        return;
    }

    // initialize the EGL display connection
    int major, minor;
    EGLBoolean result;
    result = eglInitialize(display, &major, &minor);
    if(EGL_FALSE == result) {
        emit closing("Error in eglInitialize()");
        return;
    }
//    qDebug() << "EGL version" << major << ":" << minor;

    // get an appropriate EGL frame buffer configuration
    EGLConfig config;
    EGLint num_config;
    result = eglChooseConfig(display,
                             attribute_list.constData(),
                             &config,
                             1,
                             &num_config);
    if(result == EGL_FALSE) {
        emit closing("Error in eglChooseConfig()");
        return;
    }

    // bind the OpenGL API to the EGL
    result = eglBindAPI(EGL_OPENGL_ES_API);
    if(result == EGL_FALSE) 	{
        emit closing("Error binding API");
        return;
    }

    // create an EGL rendering context
    //This seems important for using ES2
    static const EGLint context_attributes[] =
    {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    context = eglCreateContext(display,
                               config,
                               EGL_NO_CONTEXT,
                               context_attributes);
    if(context == EGL_NO_CONTEXT) {
        emit closing("Error in eglCreateContext()");
        return;
    }

    VC_RECT_T dst_rect;
    dst_rect.x      = 0;
    dst_rect.y      = 0;
    dst_rect.width  = screen_width;
    dst_rect.height = screen_height;

    VC_RECT_T src_rect;
    src_rect.x      = 0;
    src_rect.y      = 0;
    src_rect.width  = screen_width << 16;
    src_rect.height = screen_height << 16;

    // open our display with 0 being the first display,
    // there are also some other versions of this function
    // where we can pass in a mode however the mode is not
    // documented as far as I can see
    dispman_display = vc_dispmanx_display_open(0);// LCD
    // now we signal to the video core we are going to start
    // updating the config
    dispman_update  = vc_dispmanx_update_start(0);

    // this is the main setup function where we add an element
    // to the display, this is filled in to the src / dst rectangles
    dispman_element = vc_dispmanx_element_add(dispman_update,
                                              dispman_display,
                                              0, // layer
                                              &dst_rect,
                                              0, // src
                                              &src_rect,
                                              DISPMANX_PROTECTION_NONE,
                                              0, // alpha
                                              0, // clamp
                                              DISPMANX_TRANSFORM_T(0));// transform

    // having created this element we pass it to the native window
    // structure ready to create our new EGL surface
    nativewindow.element = dispman_element;
    nativewindow.width   = screen_width;
    nativewindow.height  = screen_height;
    // we now tell the vc we have finished our update
    vc_dispmanx_update_submit_sync(dispman_update);

    // finally we can create a new surface using this config and window
    surface = eglCreateWindowSurface(display, config, &nativewindow, NULL);
    if(surface == EGL_NO_SURFACE) {
        emit closing("Error in eglCreateWindowSurface()");
        return;
    }

    // connect the context to the surface
    result = eglMakeCurrent(display, surface, surface, context);
    if(EGL_FALSE == result) {
        emit closing("Error in eglMakeCurrent()");
        return;
    }
    bEglInitialized = true;
    bGLInitialized  = false;
}


void
SlideWindow::onTimerCheckInput() {
    if(mouseFd != -1) {// Read events from mouse
        rd = read(mouseFd, ev, sizeof(ev));
        if(rd > 0) {
            int count,n;
            struct input_event *evp;
            count = rd / sizeof(struct input_event);
            n = 0;
            while(count--) {
                evp = &ev[n++];
                if(evp->type == 1) {
                    if(evp->code == BTN_LEFT) {
                        if(evp->value == 1) {// Press
                            printf("Left button pressed\n");
                        }
                        else {
                            printf("Left button released\n");
                        }
                    }
                }
                if(evp->type == 2) {
                    if(evp->code == 0) {
                        // Mouse Left/Right
                        printf("Mouse moved left/right %d\n",evp->value);
                    }
                    if(evp->code == 1) {
                        // Mouse Up/Down
                        printf("Mouse moved up/down %d\n",evp->value);
                    }
                }
            }
        }
    }

    // evp->value = 0(Released), 1(Pressed), 2(Autorepeat Keypress)
    if(keyboardFd != -1) {// Read events from keyboard
        rd = read(keyboardFd, ev, sizeof(ev));
        if(rd > 0) {
            struct input_event *evp;
            int count = rd / sizeof(struct input_event);
            int n = 0;
            while(count--) {
                evp = &ev[n++];
                if(evp->type == EV_KEY) {
                    if(evp->value == 1) {
                        if((evp->code == KEY_ESC)) {
                            emit closing("Esc pressed");
                            exit(EXIT_SUCCESS);
                        }
                        if(evp->code == KEY_SPACE) {
                            timerUpdate.stop();
                            qDebug() << A << theta << angle;
                        }
                    }// if(evp->value == 1)
                }// if(evp->type == EV_KEY)
            }
        }
    }
}


void
SlideWindow::onTimerSteadyEvent() {
    updateSlideList();
    if(!bSlidesPresent) {// Still no slides !
        timerSteady.start(steadyTime);
        return;
    }
    // else..
    if(!bGLInitialized) {
        if(!initializeGL()) {
            qDebug() << "GL not initialized";
            deinitEgl();
            // emit ????
            return;
        }
    }
    timerUpdate.start(updateTime);
    t0 = 0.0;
}


void
SlideWindow::ontimerUpdateEvent() {
    if(animationType == 0) {
        A += QVector4D(0.0, -0.02, 0.0, 0.0);
        if(theta > 0.2)
            theta -= 0.04;
        else if(angle < M_PI_2)
            angle+= 0.15;
        if(A.y() < -1.88) {
            prepareNextRound();
        }
    }
    else if(animationType == 1) {
        alpha -= 0.02;
        if(alpha < 0.0) {
            prepareNextRound();
        }
    }
    else if(animationType == 2) {
        fScale -= 0.02;
        if(fScale <= 0.0) {
            prepareNextRound();
        }
    }
    else if(animationType == 3) {
            fScale -= 0.02;
            if(fScale <= 0.0) {
                prepareNextRound();
            }
    }
    else if(animationType == 4) {
            fRot += 2.0;
            if(fRot > 90.0) {
                prepareNextRound();
            }
    }
    else if(animationType == 5) {
            t0 += updateTime/1000.0;
            fRot += 2.0;
            if(fRot > 90.0) {
                prepareNextRound();
            }
    }
    paintGL();
}


bool
SlideWindow::getLocations(GLuint currentProgram) {
    vertexLocation = glGetAttribLocation(currentProgram, "p");
    texcoordLocation = glGetAttribLocation(currentProgram, "a_texcoord");
    if((vertexLocation   == -1) ||
       (texcoordLocation == -1))
    {
        emit closing("Shader attributes not found");
        return false;
    }
    // Tell OpenGL programmable pipeline how to locate vertex position data
    glVertexAttribPointer(vertexLocation,
                          4,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(VertexData),
                          0);
    glVertexAttribPointer(texcoordLocation,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(VertexData),
                          BUFFER_OFFSET(sizeof(vertex.position)));
    iTex0Loc  = glGetUniformLocation(currentProgram, "texture0");
    iMPVLoc   = glGetUniformLocation(currentProgram, "mvp_matrix");
    if((iTex0Loc == -1) ||
       (iMPVLoc  == -1))
    {
        emit closing("Shader uniforms not found");
        return false;
    }
    if(animationType == 0) {// Fold effect
        iALoc     = glGetUniformLocation(currentProgram, "a");
        iThetaLoc = glGetUniformLocation(currentProgram, "theta");
        iAngleLoc = glGetUniformLocation(currentProgram, "angle");
        iLeftLoc  = glGetUniformLocation(currentProgram, "xLeft");
        if((iALoc     == -1) ||
           (iThetaLoc == -1) ||
           (iAngleLoc == -1) ||
           (iLeftLoc  == -1))
        {
            emit closing("Shader uniforms not found");
            return false;
        }
        A     = A0;
        theta = theta0;
        angle = angle0;
        xLeft =-GLfloat(screen_width)/GLfloat(screen_height);
    }
    else if(animationType == 1) {// Fade effect
        iTex1Loc  = glGetUniformLocation(currentProgram, "texture1");
        iAlphaLoc = glGetUniformLocation(currentProgram, "alpha");
        if((iAlphaLoc == -1) ||
           (iTex0Loc == -1))
        {
            emit closing("Shader uniforms not found");
            return false;
        }
        alpha  = alpha0;
    }
    else if(animationType == 2) {// Zoom out effect
        fScale = fScale0;
    }
    else if(animationType == 3) {// Zoom out effect
        fScale = fScale0;
    }
    else if(animationType == 4) {// Rotate from bottom left effect
        fRot = fRot0;
    }
    else if(animationType == 5) {// Rotate from top left effect
        fRot = fRot0;
    }
    return true;
}


bool
SlideWindow::prepareNextRound() {
    timerUpdate.stop();
    animationType = qrand() % nAnimationTypes;
    GLuint currentProgram = programs.at(animationType);
    glUseProgram(currentProgram);
    getLocations(currentProgram);

    // Prepare the next slide...
    glDeleteTextures(1, &texture0);
    texture0 = texture1;
    glGenTextures(1, &texture1);
    if(!prepareNextSlide())
        return false;
    glBindTexture(GL_TEXTURE_2D, texture1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pBaseImage->width(), pBaseImage->height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pBaseImage->bits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    timerSteady.start(steadyTime);
    return true;
}


bool
SlideWindow::prepareNextSlide() {
    if(slideList.count() == 0) {
        emit closing("Slides removed from directory: exiting ...");
        return false;
    }
    if(iCurrentSlide >= slideList.count()) {
        iCurrentSlide = iCurrentSlide % slideList.count();
        qDebug() << "Errore: iCurrentSlide >= slideList.count()";
    }
    image.load(slideList.at(iCurrentSlide).absoluteFilePath());
    emit slideChanged(iCurrentSlide);
    image = image.scaled(screen_width, screen_height, imageMode).mirrored();
    if(pBaseImage == Q_NULLPTR) {
        pBaseImage = new QImage(screen_width, screen_height, imageFormat);
    }
    if(pBaseImage == Q_NULLPTR) {
        emit closing("Unable to create pBaseImage: exiting ...");
        return false;
    }
    QPainter painter(pBaseImage);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(0, 0, screen_width, screen_height, Qt::white);
    int x = (pBaseImage->width()-image.width())/2;
    int y = (pBaseImage->height()-image.height())/2;
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(x, y, image);
    painter.end();
    iCurrentSlide = (iCurrentSlide + 1) % slideList.count();
    return true;
}


bool
SlideWindow::initTextures() {
    // Setup the first texture
    if(!prepareNextSlide())
        return false;
    glGenTextures(1, &texture0);// Reserve a name for the first texture
    glBindTexture(GL_TEXTURE_2D, texture0);// Create a texture object
    // Specify storage and content of the texture object
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pBaseImage->width(), pBaseImage->height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pBaseImage->bits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Now the second texture
    if(!prepareNextSlide())
        return false;
    glGenTextures(1, &texture1);
    glBindTexture(GL_TEXTURE_2D, texture1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pBaseImage->width(), pBaseImage->height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pBaseImage->bits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return true;
}


void
SlideWindow::initGeometry(int screen_width, int screen_height) {
    float aspectRatio = float(screen_width)/float(screen_height);
    QVector<VertexData> vertices;
    int nxStep = 54;
    int nyStep = 36;
    float dx = 2.0/nxStep;
    float dy = 2.0/nyStep;
    float xdx, ydy;

    float x, y, xT, yT;
    x = -1.0;
    for(int i=0; i<nxStep; i++) {
        xdx = x + dx;
        if(xdx >= 1.0f) xdx= 1.0f;
        xT = x + 1.0;
        y= -1.0f;
        for(int j=0; j<nyStep; j++) {
            ydy = y + dy;
            if(ydy >= 1.0f) ydy = 1.0f;
            yT = y + 1.0;
            vertices.append({QVector4D(x*aspectRatio,   y,   0.0f, 1.0f),
                             QVector2D(0.5*xT,        0.5*yT)});
            vertices.append({QVector4D(xdx*aspectRatio, y,   0.0f, 1.0f),
                             QVector2D(0.5*(xdx+1.0), 0.5*yT)});
            vertices.append({QVector4D(x*aspectRatio,   ydy, 0.0f, 1.0f),
                             QVector2D(0.5*xT,        0.5*(ydy+1.0))});
            vertices.append({QVector4D(xdx*aspectRatio, ydy, 0.0f, 1.0f),
                             QVector2D(0.5*(xdx+1.0), 0.5*(ydy+1.0))});
            y = ydy;
        }
        x = xdx;
    }
    nVertices = vertices.count();
    // Transfer vertex data to VBO 0
    glBindBuffer(GL_ARRAY_BUFFER, arrayBuf);
    glBufferData(GL_ARRAY_BUFFER, vertices.count()*sizeof(vertices.at(0)), vertices.data(), GL_STATIC_DRAW);
}


bool
SlideWindow::initializeGL() {
    if(bGLInitialized)
        return true;
    programs.clear();
    // Generate a VBO
    glGenBuffers(1, &arrayBuf);
    // Initializes cube geometry and transfers it to VBOs
    initGeometry(screen_width, screen_height);
    if(!initShaders())
        return false;
    if(!initTextures())
        return false;

    glViewport(0, 0, (GLsizei)screen_width, (GLsizei)screen_height);
    // Reset projection matrix
    projection.setToIdentity();
    float aspectRatio   = GLfloat(screen_width)/GLfloat(screen_height);
    float verticalAngle = 2.0*atan(1.0/viewingDistance)*180.0/M_PI;
    float nearPlane     = viewingDistance - 2.0;
    float farPlane      = viewingDistance + 0.1;
    projection.perspective(verticalAngle, aspectRatio, nearPlane, farPlane);
    animationType = qrand() % nAnimationTypes;
    GLuint currentProgram = programs.at(animationType);
    glUseProgram(currentProgram);
    getLocations(currentProgram);
    bGLInitialized = true;
    return true;
}


bool
SlideWindow::compileShader(GLenum shaderType, QString shaderFile, GLuint* pShaderName) {
    GLchar *vShaderStr = ReadFile(shaderFile);
    if(vShaderStr == Q_NULLPTR)
        return false;
    *pShaderName = glCreateShader(shaderType);
    if(*pShaderName == 0) {
        emit closing("Unable to create shader");
        return false;
    }
    //load shader source
    glShaderSource(*pShaderName, 1, (const GLchar **)&vShaderStr, NULL);
    //Compile shader
    glCompileShader(*pShaderName);
    // Check the compile status
    GLint vCompiled;
    glGetShaderiv(*pShaderName, GL_COMPILE_STATUS, &vCompiled);
    if(!vCompiled) {
        emit closing("Unable to compile Fold vertex shader");
        return false;
    }
    return true;
}


bool
SlideWindow::linkProgram(GLuint* pNewProgram, GLuint vertexShader, GLuint fragmentShader) {
    *pNewProgram = glCreateProgram();
    if(*pNewProgram == 0) {
        emit closing("Unable to create Program Object");
        return false;
    }
    //Attach shaders to the program object
    glAttachShader(*pNewProgram, vertexShader);
    glAttachShader(*pNewProgram, fragmentShader);
    // Link the program
    glLinkProgram(*pNewProgram);
    // Check the link status and print the errors
    GLint linked;
    glGetProgramiv(*pNewProgram, GL_LINK_STATUS, &linked);
    if(!linked) {
        GLint infoLen = 0;
        glGetProgramiv(*pNewProgram, GL_INFO_LOG_LENGTH, &infoLen);

        if(infoLen > 1) {
            char* infoLog = (char*)malloc(sizeof(char) * infoLen);
            glGetProgramInfoLog(*pNewProgram, infoLen, NULL, infoLog);
            qDebug() <<"Error linking program" << infoLog;
            free(infoLog);
        }
        glDeleteProgram(*pNewProgram);
        emit closing("Error linking program");
        return false;
    }
    return true;
}


bool
SlideWindow::initShaders() {
    //Create vertex shader object
    GLuint vShaderFold, vShaderFade;
    if(!compileShader(GL_VERTEX_SHADER, ":/vshaderFold.glsl", &vShaderFold))
        return false;
    if(!compileShader(GL_VERTEX_SHADER, ":/vshaderFade.glsl", &vShaderFade))
        return false;

    GLuint fShaderFold, fShaderFade;
    if(!compileShader(GL_FRAGMENT_SHADER, ":/fshaderFold.glsl", &fShaderFold))
        return false;
    if(!compileShader(GL_FRAGMENT_SHADER, ":/fshaderFade.glsl", &fShaderFade))
        return false;

    // Create the program objects
    GLuint newProgram;
    if(!linkProgram(&newProgram, vShaderFold, fShaderFold))
        return false;
    programs.append(newProgram);// Fold page effect at 0
    if(!linkProgram(&newProgram, vShaderFade, fShaderFade))
        return false;
    programs.append(newProgram);// Fade  in  effect at 1
    if(!linkProgram(&newProgram, vShaderFade, fShaderFold))
        return false;
    programs.append(newProgram);// Zoom  out effect at 2
    programs.append(newProgram);// Zoom  in  effect at 3
    programs.append(newProgram);// Rotate from bottom left effect at 4
    programs.append(newProgram);// Rotate from top left effect at 5

    nAnimationTypes = programs.count();
    return true;
}


void
SlideWindow::drawGeometry() {
    glEnableVertexAttribArray(vertexLocation);
    glEnableVertexAttribArray(texcoordLocation);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, nVertices);
}


void
SlideWindow::paintGL() {
    // set the clear colour
    glClearColor(1.0, 1.0, 1.0, 1.0);
    // clear Screen and Depth Buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if(animationType == 0) {
        glClearDepthf(5.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        glUniform1i(iTex0Loc, 0);
        glUniform1i(iTex1Loc, 0);

        // Using only texture unit 0
        glActiveTexture(GL_TEXTURE0);

        glBindTexture(GL_TEXTURE_2D, texture0);
        glUniform1f(iLeftLoc, xLeft);
        glUniform1f(iAlphaLoc, 1.0f);
        glUniform4f(iALoc, A.x(), A.y(), A.z(), A.w());
        glUniform1f(iThetaLoc, theta);
        glUniform1f(iAngleLoc, angle);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance);
        // Set modelview-projection matrix
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();// Draw the geometry

        glBindTexture(GL_TEXTURE_2D, texture1);
        glUniform4f(iALoc, A0.x(), A0.y(), A0.z(), A0.w());
        glUniform1f(iThetaLoc, theta0);
        glUniform1f(iAngleLoc, angle0);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance-0.01);
        // Set modelview-projection matrix
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();
    }
    else if(animationType == 1) {
        glUniform1i(iTex0Loc, 0);
        glUniform1i(iTex1Loc, 1);
        glUniform1f(iAlphaLoc, alpha);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texture1);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance);
        // Set modelview-projection matrix
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();
    }
    else if(animationType == 2) {
        glClearDepthf(5.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        glUniform1i(iTex0Loc, 0);
        glUniform1i(iTex1Loc, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture0);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance);
        matrix.scale(fScale);
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();

        glBindTexture(GL_TEXTURE_2D, texture1);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance-0.01);
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();
    }
    else if(animationType == 3) {
        glClearDepthf(5.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        glUniform1f(iAlphaLoc, alpha0);
        glUniform1i(iTex0Loc, 0);
        glUniform1i(iTex1Loc, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture0);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance-0.01);
        // Set modelview-projection matrix
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();

        glBindTexture(GL_TEXTURE_2D, texture1);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance);
        matrix.scale(1.0f-fScale);
        // Set modelview-projection matrix
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();
    }
    else if(animationType == 4) {
        glClearDepthf(5.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        glUniform1f(iAlphaLoc, alpha0);
        glUniform1i(iTex0Loc, 0);
        glUniform1i(iTex1Loc, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture0);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance);
        matrix.translate(-1.0*GLfloat(screen_width)/GLfloat(screen_height),-1.0, 0.0);
        matrix.rotate(fRot, 0.0, 0.0, -1.0);
        matrix.translate(1.0*GLfloat(screen_width)/GLfloat(screen_height),  1.0, 0.0);
        // Set modelview-projection matrix
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();

        glBindTexture(GL_TEXTURE_2D, texture1);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance-0.01);
        // Set modelview-projection matrix
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();
    }
    else if(animationType == 5) {
        glClearDepthf(5.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);

        glUniform1f(iAlphaLoc, alpha0);
        glUniform1i(iTex0Loc, 0);
        glUniform1i(iTex1Loc, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture0);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance);
        matrix.translate(-1.0*GLfloat(screen_width)/GLfloat(screen_height), 1.0, 0.0);
        matrix.rotate(fRot, 0.0, 0.0, -1.0);
        matrix.translate(1.0*GLfloat(screen_width)/GLfloat(screen_height), -1.0, 0.0);
        // Set modelview-projection matrix
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();

        glBindTexture(GL_TEXTURE_2D, texture1);
        matrix.setToIdentity();
        matrix.translate(0.0, 0.0, -viewingDistance-0.01);
        // Set modelview-projection matrix
        glUniformMatrix4fv(iMPVLoc, 4, GL_FALSE, (projection * matrix).constData());
        drawGeometry();
    }
    // Swap back buffer to front
    eglSwapBuffers(display, surface);
}
