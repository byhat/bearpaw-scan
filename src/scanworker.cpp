#include "scanworker.h"

#include "bearpaw.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QUrl>

#include <thread>
#include <cstring>

static constexpr int kWidth = 2122;   // decoded line width (RGB)

ScanWorker::ScanWorker(QObject *parent) : QObject(parent) {}
ScanWorker::~ScanWorker() { if (m_thread.joinable()) m_thread.join(); }

void ScanWorker::findDevice() {
    const auto devices = bearpaw::Scanner::findDevices();
    if (devices.empty()) {
        m_devPath.clear();
        emit logLine("Scanner 055f:0408 not found (check the USB cable).");
        emit deviceLost();
        return;
    }
    const auto &d = devices.front();
    m_devPath = QString::fromStdString(d.id);
    emit logLine(QString("Device found: %1 [%2]")
                 .arg(QString::fromStdString(d.product), m_devPath));
    emit deviceFound(m_devPath, QString::fromStdString(d.product));
}

void ScanWorker::startScan() {
    if (m_scanning.exchange(true)) {
        emit logLine("A scan is already running");
        return;
    }
    if (m_devPath.isEmpty()) {
        findDevice();
        if (m_devPath.isEmpty()) {
            m_scanning = false;
            emit scanFailed("Scanner not found");
            return;
        }
    }
    if (m_thread.joinable())
        m_thread.join();
    m_cancel = false;
    m_thread = std::thread([this] { scanLoop(); });
}

void ScanWorker::scanLoop() {
    emit progress(0.0, "Starting");

    QByteArray rows;                       // decoded RGB lines
    rows.reserve(22 * 1024 * 1024);

    bearpaw::ScanParams params;            // color, full width
    bearpaw::Scanner scanner([this](const std::string &m) {
        emit logLine(QString::fromStdString(m));
    });

    int lineCount = 0;
    const bool ok = scanner.scan(
        params,
        [&](const uint8_t *line, int len) {
            rows.append(reinterpret_cast<const char *>(line), len);
            ++lineCount;
        },
        [&](double pct, const std::string &stage) {
            emit progress(pct, QString("Scanning: %1 MB")
                          .arg(rows.size() / 1048576.0, 0, 'f', 1));
        },
        m_cancel);

    if (!lineCount) {
        m_scanning = false;
        emit scanFailed(ok ? "Scan cancelled" : "Scan failed (see the log)");
        return;
    }

    emit progress(100.0, "Saving");
    QImage img(kWidth, lineCount, QImage::Format_RGB888);
    for (int y = 0; y < lineCount; ++y)
        std::memcpy(img.scanLine(y),
                    rows.constData() + qint64(y) * kWidth * 3, kWidth * 3);

    QString dir = QCoreApplication::applicationDirPath() + "/scans";
    QDir().mkpath(dir);
    if (!QFileInfo(dir).isWritable())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/scans",
        QDir().mkpath(dir);
    const QString name = dir + QString("/scan_%1.png")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    if (!img.save(name)) {
        m_scanning = false;
        emit scanFailed("Failed to save the PNG");
        return;
    }
    emit logLine(QString("Saved: %1 (%2x%3)").arg(name).arg(kWidth).arg(lineCount));

    m_scanning = false;
    emit scanFinished(QUrl::fromLocalFile(name).toString(),
                      kWidth, lineCount, rows.size());
}
