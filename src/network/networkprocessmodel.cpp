#include "networkprocessmodel.h"

#include <QProcess>
#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>

NetworkProcessModel::NetworkProcessModel(int intervalMs, QObject *parent)
    : QObject(parent)
{
    m_proc = new QProcess(this);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus s) { onFinished(code, static_cast<int>(s)); });

    m_timer.setInterval(intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &NetworkProcessModel::sample);
    m_timer.start();
    sample();
}

void NetworkProcessModel::sample()
{
    if (m_proc->state() != QProcess::NotRunning) {
        return; // previous run still going — skip this tick
    }
    // -t TCP, -i info (byte counters), -n numeric, -p process. -H drops the header.
    m_proc->start(QStringLiteral("ss"), {QStringLiteral("-tinpH")});
}

void NetworkProcessModel::onFinished(int /*exitCode*/, int /*status*/)
{
    parse(m_proc->readAllStandardOutput());
}

void NetworkProcessModel::parse(const QByteArray &out)
{
    static const QRegularExpression pidRe(QStringLiteral("users:\\(\\(\"([^\"]+)\",pid=(\\d+)"));
    static const QRegularExpression sentRe(QStringLiteral("bytes_sent:(\\d+)"));
    static const QRegularExpression recvRe(QStringLiteral("bytes_received:(\\d+)"));

    // Accumulate cumulative counters per pid for this snapshot.
    QHash<int, Counters> now;
    int curPid = -1;
    QString curName;
    const QList<QByteArray> lines = out.split('\n');
    for (const QByteArray &raw : lines) {
        if (raw.isEmpty()) {
            continue;
        }
        const bool continuation = raw[0] == ' ' || raw[0] == '\t';
        const QString line = QString::fromUtf8(raw);
        if (!continuation) {
            // New socket record — reset, then capture its owning process (if visible).
            curPid = -1;
            const auto m = pidRe.match(line);
            if (m.hasMatch()) {
                curName = m.captured(1);
                curPid = m.captured(2).toInt();
            }
        } else if (curPid >= 0) {
            // Info line: add this socket's cumulative bytes to its process.
            const auto ms = sentRe.match(line);
            const auto mr = recvRe.match(line);
            if (ms.hasMatch() || mr.hasMatch()) {
                Counters &c = now[curPid];
                c.name = curName;
                if (ms.hasMatch()) c.sent += ms.captured(1).toULongLong();
                if (mr.hasMatch()) c.recv += mr.captured(1).toULongLong();
            }
        }
    }

    // First snapshot just seeds the baseline.
    const qint64 elapsedMs = m_since.isValid() ? m_since.elapsed() : 0;
    m_since.restart();
    if (elapsedMs <= 0 || m_prev.isEmpty()) {
        m_prev = now;
        m_available = !now.isEmpty();
        if (m_available) {
            emit changed();
        }
        return;
    }

    struct Rate { int pid; QString name; double down; double up; };
    QList<Rate> rates;
    const double secs = static_cast<double>(elapsedMs) / 1000.0;
    for (auto it = now.constBegin(); it != now.constEnd(); ++it) {
        const auto prevIt = m_prev.constFind(it.key());
        if (prevIt == m_prev.constEnd()) {
            continue; // new pid this round — needs two samples for a rate
        }
        // Counters only ever grow for a live socket; guard against socket churn resetting them.
        const quint64 dSent = it.value().sent >= prevIt.value().sent ? it.value().sent - prevIt.value().sent : 0;
        const quint64 dRecv = it.value().recv >= prevIt.value().recv ? it.value().recv - prevIt.value().recv : 0;
        if (dSent == 0 && dRecv == 0) {
            continue;
        }
        rates.append({it.key(), it.value().name,
                      static_cast<double>(dRecv) / secs, static_cast<double>(dSent) / secs});
    }
    m_prev = now;

    std::sort(rates.begin(), rates.end(),
              [](const Rate &a, const Rate &b) { return (a.down + a.up) > (b.down + b.up); });

    QVariantList top;
    for (int i = 0; i < rates.size() && i < 5; ++i) {
        top.append(QVariantMap{
            {QStringLiteral("pid"), rates[i].pid},
            {QStringLiteral("name"), rates[i].name},
            {QStringLiteral("download"), rates[i].down},
            {QStringLiteral("upload"), rates[i].up},
        });
    }
    m_top = top;
    m_available = true;
    emit changed();
}
