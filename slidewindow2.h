#ifndef SLIDEWINDOW_H
#define SLIDEWINDOW_H

#include <QObject>

#include <QTimer>
#include <QFileInfoList>
#include <QImage>
#include <QVector4D>
#include <QVector2D>
#include <QMatrix4x4>

#include "GLES2/gl2.h"
#include "GLES2/gl2ext.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"
#include "bcm_host.h"

#include <linux/input.h>

class SlideWindow : public QObject
{
    Q_OBJECT
public:
    SlideWindow();
    ~SlideWindow();
    void paintGL();
    void pauseSlideShow();
    bool isReady();
    bool isRunning();
    void initEgl();
    void deinitEgl();
    bool initializeGL();

public Q_SLOTS:
    void setSlideDir(QString sDir);
    void startSlideShow();
    void stopSlideShow();
    void exitShow();

Q_SIGNALS:
    void crashed();

signals:
    void closing(QString sReason);
    void slideChanged(int iCurrentSlide);

public slots:
    void ontimerUpdateEvent();
    void onTimerSteadyEvent();
    void onTimerCheckInput();

protected:
    void initEglAttributes();
    void drawGeometry();

    void updateSlideList();
    bool prepareNextRound() ;
    bool prepareNextSlide();

    bool compileShader(GLenum shaderType, QString shaderFile, GLuint *pShaderName);
    bool linkProgram(GLuint* pNewProgram, GLuint vertexShader, GLuint fragmentShader);
    bool initShaders();
    bool initTextures();
    void initGeometry(int screen_width, int screen_height);
    bool getLocations(GLuint currentProgram);

    void initInputDevices();
    void releaseInputDevices();

    char* ReadFile(QString url);

private:
    uint32_t screen_width;
    uint32_t screen_height;
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    EGL_DISPMANX_WINDOW_T nativewindow;
    // OpenGL|ES objects
    DISPMANX_DISPLAY_HANDLE_T dispman_display;
    DISPMANX_ELEMENT_HANDLE_T dispman_element;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    QVector<EGLint> attribute_list;

private:
    QApplication* pMyApplication;
    QString sSlideDir;
    QFileInfoList slideList;

    QTimer timerUpdate, timerSteady;
    QTimer timerCheckInput;

    int iCurrentSlide;
    QImage::Format imageFormat;
    QImage *pBaseImage, image;
    enum Qt::AspectRatioMode imageMode;

    int steadyTime;
    int updateTime;

    struct VertexData {
        QVector4D position;
        QVector2D texCoord;
    } vertex;
    GLuint arrayBuf;
    int nVertices;

    QVector<GLuint> programs;

    GLfloat viewingDistance;
    QMatrix4x4 matrix;
    GLuint texture0, texture1;
    QMatrix4x4 projection;
    QVector4D A,      A0;
    GLfloat   theta,  theta0;
    GLfloat   angle,  angle0;
    GLfloat   alpha,  alpha0;
    GLfloat   fScale, fScale0;
    GLfloat   fRot,   fRot0;
    GLfloat   xLeft;

    GLint animationType;
    int nAnimationTypes;

    GLint iLeftLoc;
    GLint iAlphaLoc, iALoc, iThetaLoc, iAngleLoc;
    GLint iTex0Loc, iTex1Loc;
    GLint iMPVLoc;
    GLint vertexLocation;
    GLint texcoordLocation;

    int mouseFd;
    int keyboardFd;
    struct input_event ev[64];
    int rd;
    double t0;
    bool bGLInitialized, bEglInitialized, bSlidesPresent, bRunning;
};

#endif // SLIDEWINDOW_H
