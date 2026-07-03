#pragma once

#include "config.h"

#include <QObject>
#include <QRect>

#include <functional>

class WindowManagerBackend;

// Fallback glue for running the bar as a plain window (no Wayland layer-shell): drives
// i3/sway into treating it as a bar — floating, sticky, borderless — and pins it to the
// bar geometry. Window creation races the WM's IPC visibility, hence schedule()'s retry
// ladder; on native sway the bar is later re-targeted precisely via its [con_id=…] once
// the tree snapshot finds it (handleQbarNodeFound).
class TestWindowRules final : public QObject {
    Q_OBJECT

public:
    // `config` is the owning bar's live config; `barGeometry` yields the target rect
    // (the owner computes it from screen + config).
    TestWindowRules(WindowManagerBackend *wm, const BarConfig &config,
                    std::function<QRect()> barGeometry, QObject *parent = nullptr);

    void schedule();

public slots:
    void handleQbarNodeFound(qint64 nodeId);

private slots:
    void applyRules();
    void moveWindow();

private:
    QString criteria() const;
    void installRule();

    WindowManagerBackend *m_wm = nullptr;
    const BarConfig &m_config;
    std::function<QRect()> m_barGeometry;
    qint64 m_swayNodeId = -1;
};
