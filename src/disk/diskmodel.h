#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

// Disk usage of a single mount point (waybar's "disk" module). Reads QStorageInfo
// (statvfs) on a slow timer — disk fill changes gradually, so polling is cheap.
class DiskModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int percent READ percent NOTIFY changed)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString displayText READ displayText NOTIFY changed)
    Q_PROPERTY(QString tooltipText READ tooltipText NOTIFY changed)

public:
    explicit DiskModel(QObject *parent = nullptr);

    bool available() const { return m_available; }
    int percent() const { return m_percent; }
    QString path() const { return m_path; }
    void setPath(const QString &path);
    QString displayText() const { return m_displayText; }
    QString tooltipText() const { return m_tooltipText; }

signals:
    void changed();
    void pathChanged();

private:
    void refresh();
    static QString humanSize(qint64 bytes);

    QString m_path = QStringLiteral("/");
    bool m_available = false;
    int m_percent = 0;
    QString m_displayText;
    QString m_tooltipText;
    QTimer m_timer;
};
