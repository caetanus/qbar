#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

class QProcess;

// Per-process network throughput via `ss -tinp` (the inet_diag CLI), sampled asynchronously
// every few seconds. `ss` already maps sockets→PIDs, so there's no /proc scan, no packet
// capture, no lsof, no netlink parsing — and it runs in a child process, off the GUI thread.
// Limitations: TCP only (UDP/QUIC carry no per-socket byte counters); without root only the
// current user's own processes are attributed.
class NetworkProcessModel final : public QObject {
    Q_OBJECT
    // Top talkers this interval, each: { pid, name, download (B/s), upload (B/s) }.
    Q_PROPERTY(QVariantList topTalkers READ topTalkers NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)

public:
    explicit NetworkProcessModel(int intervalMs = 5000, QObject *parent = nullptr);

    QVariantList topTalkers() const { return m_top; }
    bool available() const { return m_available; }

signals:
    void changed();

private slots:
    void sample();
    void onFinished(int exitCode, int status);

private:
    struct Counters {
        quint64 sent = 0; // cumulative bytes_sent (upload)
        quint64 recv = 0; // cumulative bytes_received (download)
        QString name;
    };

    void parse(const QByteArray &out);

    QTimer m_timer;
    QProcess *m_proc = nullptr;
    QHash<int, Counters> m_prev; // pid → cumulative bytes at the last sample
    QElapsedTimer m_since;
    QVariantList m_top;
    bool m_available = false;
};
