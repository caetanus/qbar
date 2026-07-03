#include "baractions.h"

#include "caffeine/caffeinemodel.h"
#include "qml/modelcapsules.h"
#include "qml/qbarpopupservice.h"
#include "wm/windowmanagerbackend.h"

#include <QDate>
#include <QDebug>
#include <QGuiApplication>
#include <QPoint>
#include <QPointF>
#include <QProcess>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace {

int clampCoordinate(int value, int minValue, int maxValue)
{
    return std::clamp(value, minValue, std::max(minValue, maxValue));
}

QPoint clampedPopupPosition(const QRect &screenArea, const QPoint &preferred, const QSize &popupSize)
{
    const int maxX = screenArea.x() + screenArea.width() - popupSize.width();
    const int maxY = screenArea.y() + screenArea.height() - popupSize.height();
    return QPoint(clampCoordinate(preferred.x(), screenArea.x(), maxX),
                  clampCoordinate(preferred.y(), screenArea.y(), maxY));
}

} // namespace

BarActions::BarActions(QQuickView *window, WindowManagerBackend *wm, const BarConfig &config,
                       QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_wm(wm)
    , m_config(config)
    , m_evolutionCalendarExecutable(QStandardPaths::findExecutable(QStringLiteral("evolution")))
{
}

void BarActions::setPopupService(QBarPopupService *service)
{
    m_popupService = service;
    if (m_popupService != nullptr) {
        connect(m_popupService, &QBarPopupService::popupClosed, this, [this](const QString &id) {
            if (id == m_calendarPopupId) {
                m_calendarPopupId.clear();
            }
        });
    }
}

bool BarActions::calendarAppAvailable() const
{
    return !m_evolutionCalendarExecutable.isEmpty();
}

void BarActions::openCalendar(QObject *anchorObject)
{
    if (m_popupService == nullptr) {
        return;
    }

    if (!m_calendarPopupId.isEmpty()) {
        m_popupService->closePopup(m_calendarPopupId);
        m_calendarPopupId.clear();
        return;
    }

    const int popupWidth = 560;
    const int popupHeight = 380;
    const QSize popupSize(popupWidth, popupHeight);
    const int gap = 0;
    auto *anchorItem = qobject_cast<QQuickItem *>(anchorObject);
    QPoint anchorTopLeft = m_window->mapToGlobal(QPoint(0, 0));
    QPoint anchorBottomRight = m_window->mapToGlobal(QPoint(m_window->width(), m_window->height()));

    if (anchorItem != nullptr) {
        const QPointF itemTopLeft = anchorItem->mapToScene(QPointF(0.0, 0.0));
        const QPointF itemBottomRight = anchorItem->mapToScene(QPointF(anchorItem->width(), anchorItem->height()));
        anchorTopLeft = m_window->mapToGlobal(QPoint(qRound(itemTopLeft.x()), qRound(itemTopLeft.y())));
        anchorBottomRight = m_window->mapToGlobal(QPoint(qRound(itemBottomRight.x()), qRound(itemBottomRight.y())));
    }

    int x = anchorBottomRight.x() - popupWidth;
    int y = m_config.position == BarPosition::Bottom
        ? anchorTopLeft.y() - popupHeight - gap
        : anchorBottomRight.y() + gap;

    const QScreen *screen = QGuiApplication::screenAt(anchorTopLeft);
    if (screen != nullptr) {
        const QPoint clamped = clampedPopupPosition(screen->availableGeometry(), QPoint(x, y), popupSize);
        x = clamped.x();
        y = clamped.y();
    }

    QVariantMap properties;
    properties.insert(QStringLiteral("selectedDate"), QDate::currentDate());
    m_calendarPopupId = m_popupService->openPopup(QUrl(QStringLiteral("qrc:/popups/CalendarPopup.qml")),
                                                  properties,
                                                  x,
                                                  y,
                                                  popupWidth,
                                                  popupHeight,
                                                  QStringLiteral("calendar"));
    if (m_calendarPopupId.isEmpty()) {
        qWarning() << "QBar calendar popup failed to open";
    }
}

void BarActions::openEvolutionCalendar()
{
    if (!calendarAppAvailable()) {
        return;
    }

    const QStringList arguments = {QStringLiteral("--component=calendar")};
    if (!QProcess::startDetached(m_evolutionCalendarExecutable, arguments)) {
        qWarning() << "QBar failed to launch Evolution Calendar" << m_evolutionCalendarExecutable << arguments;
    }
}

void BarActions::cycleKeyboardLayout()
{
    if (m_wm != nullptr) {
        m_wm->cycleKeyboardLayout();
    }
}

void BarActions::toggleCaffeine()
{
    auto *model = qobject_cast<CaffeineModel *>(
        ModelCapsules::instance()->acquire(QStringLiteral("caffeine"), m_window));
    if (model != nullptr) {
        model->toggle();
    }
}
