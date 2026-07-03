#include "testwindowrules.h"

#include "windowmanagerbackend.h"

#include <QDebug>
#include <QTimer>

#include <utility>

TestWindowRules::TestWindowRules(WindowManagerBackend *wm, const BarConfig &config,
                                 std::function<QRect()> barGeometry, QObject *parent)
    : QObject(parent)
    , m_wm(wm)
    , m_config(config)
    , m_barGeometry(std::move(barGeometry))
{
}

QString TestWindowRules::criteria() const
{
    if (m_swayNodeId >= 0) {
        return QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
    }

    return QStringLiteral("[class=\"qbar\"]; [instance=\"qbar\"]; [title=\"QBar\"]");
}

void TestWindowRules::applyRules()
{
    if (m_config.waylandLayerShell || m_wm == nullptr) {
        return;
    }

    if (m_swayNodeId >= 0) {
        const QString criteria = QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
        m_wm->runCommand(QStringLiteral("%1 floating enable; %1 sticky enable; %1 border none").arg(criteria));
        QTimer::singleShot(80, this, SLOT(moveWindow()));
        return;
    }

    // i3 and sway (for XWayland windows) both accept class/instance/title
    // criteria. Neither accepts the i3-invalid [pid=…] nor the wayland-only
    // [app_id=…]; on i3 those are unknown tokens that make it reject the whole
    // batched command as a parse error, so the bar never gets floated/unbordered.
    // The native-sway case is handled above via [con_id=…].
    const QString classCriteria = QStringLiteral("[class=\"qbar\"]");
    const QString instanceCriteria = QStringLiteral("[instance=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    m_wm->runCommand(QStringLiteral(
                         "%1 floating enable; %1 sticky enable; %1 border none; "
                         "%2 floating enable; %2 sticky enable; %2 border none; "
                         "%3 floating enable; %3 sticky enable; %3 border none")
                         .arg(classCriteria, instanceCriteria, titleCriteria));
    QTimer::singleShot(80, this, SLOT(moveWindow()));
}

void TestWindowRules::moveWindow()
{
    if (m_config.waylandLayerShell || m_wm == nullptr) {
        return;
    }

    const QRect target = m_barGeometry();
    qWarning() << "QBar test window target geometry:" << target;
    const int moveY = target.y();
    if (m_swayNodeId >= 0) {
        const QString criteria = QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
        m_wm->runCommand(QStringLiteral("%1 resize set width %2 px height %3 px; %1 move absolute position %4 %5")
                             .arg(criteria)
                             .arg(target.width())
                             .arg(target.height())
                             .arg(target.x())
                             .arg(moveY));
        return;
    }

    const QString classCriteria = QStringLiteral("[class=\"qbar\"]");
    const QString instanceCriteria = QStringLiteral("[instance=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    m_wm->runCommand(QStringLiteral(
                         "%1 resize set width %2 px height %3 px; %1 move absolute position %4 %5; "
                         "%6 resize set width %2 px height %3 px; %6 move absolute position %4 %5; "
                         "%7 resize set width %2 px height %3 px; %7 move absolute position %4 %5")
                         .arg(classCriteria)
                         .arg(target.width())
                         .arg(target.height())
                         .arg(target.x())
                         .arg(moveY)
                         .arg(instanceCriteria)
                         .arg(titleCriteria));
}

void TestWindowRules::handleQbarNodeFound(qint64 nodeId)
{
    m_swayNodeId = nodeId;
    applyRules();
    QTimer::singleShot(80, this, SLOT(moveWindow()));
}

void TestWindowRules::installRule()
{
    if (m_config.waylandLayerShell || m_wm == nullptr) {
        return;
    }

    const QRect target = m_barGeometry();
    const QString classCriteria = QStringLiteral("[class=\"qbar\"]");
    const QString instanceCriteria = QStringLiteral("[instance=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    Q_UNUSED(target);
    const QString action = QStringLiteral("floating enable, sticky enable, border none");

    m_wm->runCommand(QStringLiteral("for_window %1 %2; for_window %3 %2; for_window %4 %2")
                         .arg(classCriteria, action, instanceCriteria, titleCriteria));
}

void TestWindowRules::schedule()
{
    if (m_config.waylandLayerShell) {
        return;
    }

    applyRules();
    QTimer::singleShot(120, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(150, this, SLOT(applyRules()));
    QTimer::singleShot(350, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(500, this, SLOT(applyRules()));
    QTimer::singleShot(900, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(1200, this, SLOT(applyRules()));
    QTimer::singleShot(1500, m_wm, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(1800, this, SLOT(moveWindow()));
}
