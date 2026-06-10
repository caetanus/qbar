#pragma once

#include "config.h"

#include <QCalendarWidget>
#include <QFrame>
#include <QWidget>

class QHBoxLayout;

class BarWindow final : public QWidget {
    Q_OBJECT

public:
    explicit BarWindow(const BarConfig &config, QWidget *parent = nullptr);

private slots:
    void handleAppletActivated(const QString &appletName);

private:
    void configureWindow();
    void buildLayout();
    void positionAtTop();
    void toggleCalendar();

    BarConfig m_config;
    QHBoxLayout *m_layout = nullptr;
    QFrame *m_calendarPopup = nullptr;
    QCalendarWidget *m_calendar = nullptr;
};
