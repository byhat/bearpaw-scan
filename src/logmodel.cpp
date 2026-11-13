#include "logmodel.h"
#include <QDateTime>
#include <QDir>
#include <QMetaObject>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QFileInfo>

LogModel::LogModel(QObject *parent) : QAbstractListModel(parent) {
    QString dir = QCoreApplication::applicationDirPath() + "/logs";
    QDir().mkpath(dir);
    QFileInfo probe(dir);
    if (!probe.isDir() || !probe.isWritable())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs",
        QDir().mkpath(dir);
    m_file.setFileName(QDir(dir).filePath(
        QString("bearpawscan_%1.log").arg(QDateTime::currentDateTime().toString("yyyyMMdd"))));
    m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    m_stream.setDevice(&m_file);
}

int LogModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_lines.size();
}

QVariant LogModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_lines.size())
        return {};
    return role == TextRole ? m_lines.at(idx.row()) : QVariant();
}

QHash<int, QByteArray> LogModel::roleNames() const {
    return { {TextRole, "text"} };
}

void LogModel::append(const QString &line) {
    // файл — синхронно в потоке-источнике: строка попадёт на диск даже при крэше
    QString stamped = QDateTime::currentDateTime().toString("hh:mm:ss.zzz") + "  " + line;
    {
        QMutexLocker lock(&m_mutex);
        m_stream << stamped << Qt::endl;
        m_stream.flush();
    }
    // модель — queued в GUI-поток (append может зваться из рабочего потока)
    QMetaObject::invokeMethod(this, [this, stamped] { insertLine(stamped); },
                              Qt::QueuedConnection);
}

void LogModel::insertLine(const QString &stamped) {
    int rows = m_lines.size();
    beginInsertRows({}, rows, rows);
    m_lines << stamped;
    endInsertRows();
    if (m_lines.size() > kMaxLines) {           // не растим модель бесконечно
        beginRemoveRows({}, 0, m_lines.size() - kMaxLines - 1);
        while (m_lines.size() > kMaxLines)
            m_lines.removeFirst();
        endRemoveRows();
    }
}
