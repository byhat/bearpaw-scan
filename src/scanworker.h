#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <thread>

/*
 * ScanWorker — GUI-side wrapper over the bearpaw::Scanner engine
 * (vendored libbearpawcore; protocol reverse-engineered, see the
 * bearpaw-sane driver project).
 */
class ScanWorker : public QObject {
    Q_OBJECT
public:
    explicit ScanWorker(QObject *parent = nullptr);
    ~ScanWorker();

public slots:
    void findDevice();   // sysfs/libusb discovery
    void startScan();    // full scan cycle in a worker thread

signals:
    void logLine(const QString &msg);
    void progress(qreal pct, const QString &stage);
    void deviceFound(const QString &path, const QString &desc);
    void deviceLost();
    void scanFinished(const QString &pngPath, int w, int h, qint64 bytes);
    void scanFailed(const QString &reason);

private:
    void scanLoop();

    QString m_devPath;      // last seen "bus:addr" (informational)
    std::atomic_bool m_scanning{false};
    std::atomic_bool m_cancel{false};
    std::thread m_thread;   // the scan runs here; the object stays in the GUI thread
};
