#pragma once

#include "config.h"

#include <QObject>
#include <QString>

class QBarPopupService;
class QQuickView;
class WindowManagerBackend;

// The bar's user-facing actions (clicks / IPC): the anchored calendar popup, launching
// Evolution, keyboard-layout cycling and the caffeine toggle. Invoked from QML through
// BarWindow's forwarding slots, so the QML contract stays on `barWindow`.
class BarActions final : public QObject {
    Q_OBJECT

public:
    BarActions(QQuickView *window, WindowManagerBackend *wm, const BarConfig &config,
               QObject *parent = nullptr);

    // The popup service is created later (buildLayout); hooking it here also moves the
    // popupClosed bookkeeping for the calendar popup id.
    void setPopupService(QBarPopupService *service);
    bool calendarAppAvailable() const;

public slots:
    void openCalendar(QObject *anchorObject);
    void openEvolutionCalendar();
    void cycleKeyboardLayout();
    void toggleCaffeine();

private:
    QQuickView *m_window = nullptr;
    WindowManagerBackend *m_wm = nullptr;
    const BarConfig &m_config;
    QBarPopupService *m_popupService = nullptr;
    QString m_calendarPopupId;
    QString m_evolutionCalendarExecutable;
};
