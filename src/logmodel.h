#pragma once
#include <QAbstractListModel>
#include <QStringList>
#include <QFile>
#include <QMutex>
#include <QTextStream>

/* Модель лога для QML: строки в ListView + параллельная запись в файл. */
class LogModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { TextRole = Qt::UserRole + 1 };
    explicit LogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QString logFilePath() const { return m_file.fileName(); }

public slots:
    void append(const QString &line);   // потокобезопасно (может зваться из worker-потока)

private:
    void insertLine(const QString &stamped);  // в GUI-потоке

private:
    QStringList m_lines;
    QFile m_file;
    QTextStream m_stream;
    QMutex m_mutex;
    static constexpr int kMaxLines = 5000;
};
