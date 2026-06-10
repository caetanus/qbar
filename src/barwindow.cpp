#include "barwindow.h"

#include "applethost.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>

BarWindow::BarWindow(const BarConfig &config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
    configureWindow();
    buildLayout();
    positionAtTop();
}

void BarWindow::configureWindow()
{
    setWindowTitle(QStringLiteral("QBar"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setStyleSheet(QStringLiteral(
                      "QWidget#BarSurface {"
                      "background: %1;"
                      "border-radius: 0px;"
                      "color: %2;"
                      "font-family: \"%3\";"
                      "font-size: %4pt;"
                      "}"
                      "QFrame#CalendarPopup {"
                      "background: %1;"
                      "border: 1px solid rgba(255, 255, 255, 48);"
                      "border-radius: 2px;"
                      "}"
                      "QCalendarWidget { color: %2; }")
                      .arg(m_config.background.name(QColor::HexArgb),
                           m_config.foreground.name(QColor::HexArgb),
                           m_config.fontFamily)
                      .arg(m_config.fontSize));
}

void BarWindow::buildLayout()
{
    auto *surface = new QWidget(this);
    surface->setObjectName(QStringLiteral("BarSurface"));

    m_layout = new QHBoxLayout(surface);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(m_config.spacing);

    for (const auto &name : m_config.applets) {
        auto *host = new AppletHost(name, m_config, surface);
        connect(host, SIGNAL(activated(QString)), this, SLOT(handleAppletActivated(QString)));
        m_layout->addWidget(host);

        if (name == QStringLiteral("Title")) {
            m_layout->setStretch(m_layout->count() - 1, 1);
        }
    }

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(m_config.margin, 0, m_config.margin, 0);
    outer->addWidget(surface);

    m_calendarPopup = new QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint);
    m_calendarPopup->setObjectName(QStringLiteral("CalendarPopup"));
    m_calendarPopup->setAttribute(Qt::WA_TranslucentBackground, true);

    auto *popupLayout = new QVBoxLayout(m_calendarPopup);
    popupLayout->setContentsMargins(10, 10, 10, 10);
    m_calendar = new QCalendarWidget(m_calendarPopup);
    popupLayout->addWidget(m_calendar);
}

void BarWindow::positionAtTop()
{
    const auto *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        resize(900, m_config.height);
        return;
    }

    const auto area = screen->availableGeometry();
    setGeometry(area.x(), area.y(), area.width(), m_config.height);
}

void BarWindow::handleAppletActivated(const QString &appletName)
{
    if (appletName == QStringLiteral("Clock") || appletName == QStringLiteral("Calendar")) {
        toggleCalendar();
    }
}

void BarWindow::toggleCalendar()
{
    if (m_calendarPopup->isVisible()) {
        m_calendarPopup->hide();
        return;
    }

    const int popupWidth = 340;
    m_calendarPopup->resize(popupWidth, 280);
    const auto globalTopRight = mapToGlobal(rect().topRight());
    m_calendarPopup->move(globalTopRight.x() - popupWidth - m_config.margin, globalTopRight.y() + m_config.height + 4);
    m_calendarPopup->show();
}
