#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QFile>
#include <unistd.h>

#include "logmodel.h"
#include "scanworker.h"

static LogModel *g_log = nullptr;
static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    if (g_log)
        g_log->append(QString("[%1] %2")
                      .arg(type == QtDebugMsg ? "qml" : "qt").arg(msg));
}

/*
 * Running under sudo breaks the graphics stack: the Wayland socket is not
 * accessible to root, and without `sudo -E` DISPLAY/XAUTHORITY are lost.
 * We fix it ourselves so the app can be started with a plain
 * `sudo ./bearpaw-scan` (USB access needs root unless a udev rule is set).
 */
static void fixRootDisplayEnv() {
    if (::geteuid() != 0)
        return;
    if (qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland"
        && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");   // via XWayland
    if (qEnvironmentVariable("DISPLAY").isEmpty())
        qputenv("DISPLAY", ":0");
    if (qEnvironmentVariable("XAUTHORITY").isEmpty()) {
        QDir home("/home");
        for (const QString &u : home.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString x = "/home/" + u + "/.Xauthority";
            if (QFile::exists(x)) { qputenv("XAUTHORITY", x.toLocal8Bit()); break; }
        }
    }
}

int main(int argc, char *argv[]) {
    fixRootDisplayEnv();
    // system QML styles (e.g. KDE Breeze) can crash on ProgressBar —
    // pin the built-in Basic style for predictable behaviour everywhere
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE"))
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QGuiApplication app(argc, argv);
    app.setApplicationName("bearpaw-scan");
    app.setOrganizationName("byhat");

    LogModel *logModel = new LogModel(&app);
    g_log = logModel;
    qInstallMessageHandler(messageHandler);

    // the object lives in the GUI thread (QML Connections require that);
    // the scan itself runs inside a std::thread, signals arrive queued
    ScanWorker *worker = new ScanWorker(&app);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("worker", worker);
    engine.rootContext()->setContextProperty("logModel", logModel);
    engine.rootContext()->setContextProperty(
        "autotest", qEnvironmentVariableIsSet("BEARPAW_AUTOTEST") ? true : false);
    QObject::connect(worker, &ScanWorker::logLine, logModel, &LogModel::append);

    engine.load(QUrl("qrc:/BearPawScan/src/main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    logModel->append("bearpaw-scan started. Log file: " + logModel->logFilePath());
    return app.exec();
}
