#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include "slidewindow2.h"
#include "slidewindow_adaptor.h"
#include "unistd.h"

int iCurrentSlide;

class MyApp: public QCoreApplication
{
public:
    MyApp (int argc, char *argv[]);
    bool autoStart = false;
    int exec();

private:
    SlideWindow *pSlideWindow;
    QString sSlideDir;
};


MyApp::MyApp(int argc, char *argv[])
    : QCoreApplication(argc, argv)
{
    pSlideWindow = new SlideWindow();
    new SlideShowInterfaceAdaptor(pSlideWindow);
    QDBusConnection connection = QDBusConnection::sessionBus();  // Bus
    if(!connection.registerObject("/SlideShow", pSlideWindow)) { // Path
        qCritical() << Q_FUNC_INFO
                    << "connection.registerObject() Failed !";
        exit(EXIT_FAILURE);
    }
    if(!connection.registerService("org.salvato.gabriele.slideshow")) {// Service name
        qCritical() << Q_FUNC_INFO
                    << "connection.registerService() Failed !";
        exit(EXIT_FAILURE);
    }
    sSlideDir = QDir::homePath()+QString("/slides");
    iCurrentSlide = 0;
    autoStart = false;
    int c;
    while ((c = getopt(argc, argv, "d:g")) != -1) {
        switch (c)
        {
            case 'd':
                sSlideDir = QString(optarg);
                break;
            case 'g':
                autoStart = true;
                break;
            default:
                break;
        }
    }
}


int
MyApp::exec() {
    if(!autoStart)
        return QCoreApplication::exec();
    if(QDir(sSlideDir).exists()) {
        pSlideWindow->setSlideDir(sSlideDir);
        pSlideWindow->startSlideShow();
        return QCoreApplication::exec();
    }
    else {
        qCritical() << "Unexisting Slide Directory" << sSlideDir << "...Exiting...";
        return EXIT_FAILURE;
    }
}


int
main(int argc, char *argv[]) {
    MyApp a(argc, argv);
    int iResult = a.exec();
    return iResult;
}
