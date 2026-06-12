#include "barwindow.h"

#include "qml/qbarpopupservice.h"
#include "cpu/cpumodel.h"
#include "temperature/temperaturemodel.h"
#include "brightness/brightnessmodel.h"
#include "caffeine/caffeinemodel.h"
#include "battery/batterymodel.h"
#include "network/networkmodel.h"
#include "networkmanager/networkmanagermodel.h"
#include "sound/soundmodel.h"
#include "platform/platformbarintegration.h"

#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHideEvent>
#include <QIcon>
#include <QPainter>
#include <QPoint>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QPixmap>
#include <QScreen>
#include <QShowEvent>
#include <QPointF>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <unistd.h>

namespace {

class ThemeIconProvider final : public QQuickImageProvider {
public:
    ThemeIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        const int side = std::max(1, requestedSize.width() > 0 ? requestedSize.width() : 18);
        const QStringList parts = id.split(QLatin1Char('|'));
        const QString iconId = parts.value(0);
        const QColor tintColor = parts.size() > 1 ? QColor(parts.value(1)) : QColor();
        const QString iconThemePath = parts.size() > 2 ? parts.value(2) : QString();

        QIcon icon;
        if (QFileInfo::exists(iconId)) {
            icon = QIcon(iconId);
        } else {
            icon = QIcon::fromTheme(iconId);
            if (icon.isNull()) {
                const QString fileName = findIconFile(iconId, iconThemePath);
                if (!fileName.isEmpty()) {
                    icon = QIcon(fileName);
                }
            }
        }
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        }

        QPixmap pixmap = icon.pixmap(side, side);
        const QSize actualSize = pixmap.size();
        const int displaySide = std::min(side, std::max(actualSize.width(), actualSize.height()));
        if (displaySide < side) {
            pixmap = pixmap.scaled(displaySide, displaySide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        if (tintColor.isValid() && tintColor.alpha() > 0) {
            QPixmap tinted(displaySide, displaySide);
            tinted.fill(Qt::transparent);
            QPainter painter(&tinted);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.drawPixmap(0, 0, pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(tinted.rect(), tintColor);
            painter.end();
            pixmap = tinted;
        }

        if (size != nullptr) {
            *size = pixmap.size();
        }
        return pixmap;
    }

private:
    QString findIconFile(const QString &iconName, const QString &iconThemePath) const
    {
        QStringList iconNames;
        if (iconName.endsWith(QStringLiteral("-symbolic"))) {
            iconNames << iconName.left(iconName.size() - 9);
        }
        iconNames << iconName;

        QStringList names;
        for (const QString &name : iconNames) {
            names << name + QStringLiteral(".png")
                  << name + QStringLiteral(".svg")
                  << name + QStringLiteral(".xpm");
        }

        const QStringList themeNames = {
            QIcon::themeName(),
            QIcon::fallbackThemeName(),
            QStringLiteral("hicolor"),
        };

        auto pickLargest = [&](QDirIterator &it) -> QString {
            QString best;
            qint64 bestSize = 0;
            while (it.hasNext()) {
                const QString path = it.next();
                const qint64 sz = QFileInfo(path).size();
                if (sz > bestSize) {
                    best = path;
                    bestSize = sz;
                }
            }
            return best;
        };

        auto findInBases = [&](const QStringList &bases) -> QString {
            for (const QString &base : bases) {
                QDirIterator looseIterator(base, names, QDir::Files, QDirIterator::Subdirectories);
                const QString found = pickLargest(looseIterator);
                if (!found.isEmpty()) {
                    return found;
                }
            }
            for (const QString &theme : themeNames) {
                if (theme.isEmpty()) {
                    continue;
                }
                for (const QString &base : bases) {
                    const QString root = base + QLatin1Char('/') + theme;
                    if (!QFileInfo::exists(root)) {
                        continue;
                    }
                    QDirIterator iterator(root, names, QDir::Files, QDirIterator::Subdirectories);
                    const QString found = pickLargest(iterator);
                    if (!found.isEmpty()) {
                        return found;
                    }
                }
            }
            return {};
        };

        QStringList homeBases;
        QStringList systemBases;
        if (!iconThemePath.isEmpty()) {
            homeBases.prepend(iconThemePath);
            systemBases.prepend(iconThemePath);
        }
        for (const QString &base : QIcon::themeSearchPaths()) {
            if (base.startsWith(QDir::homePath())) {
                homeBases.append(base);
            } else {
                systemBases.append(base);
            }
        }

        const QString homeIcon = findInBases(homeBases);
        if (!homeIcon.isEmpty()) {
            return homeIcon;
        }

        const QString systemIcon = findInBases(systemBases);
        if (!systemIcon.isEmpty()) {
            return systemIcon;
        }

        return {};
    }
};

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

static QColor contrastColorFor(const QColor &background)
{
    const double luminance = (0.2126 * background.redF())
        + (0.7152 * background.greenF())
        + (0.0722 * background.blueF());
    return luminance < 0.5 ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1f2933"));
}

QVariantMap themeMap(const BarConfig &config)
{
    return {
        {QStringLiteral("background"), config.background.name(QColor::HexArgb)},
        {QStringLiteral("foreground"), config.foreground.name(QColor::HexArgb)},
        {QStringLiteral("accent"), config.accent.name(QColor::HexArgb)},
        {QStringLiteral("contrastIcon"), contrastColorFor(config.background).name(QColor::HexArgb)},
        {QStringLiteral("fontFamily"), config.fontFamily},
        {QStringLiteral("fontSize"), config.fontSize},
        {QStringLiteral("trayItemPadding"), config.trayItemPadding},
        {QStringLiteral("animationDuration"), config.animationDuration},
        {QStringLiteral("animationEasing"), static_cast<int>(config.animationEasing.type())},
        {QStringLiteral("height"), config.height},
        {QStringLiteral("margin"), config.margin},
        {QStringLiteral("spacing"), config.spacing},
    };
}

} // namespace

BarWindow::BarWindow(const BarConfig &config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
    configureWindow();
    m_i3Ipc = new I3IpcClient(this);
    m_trayModel = new StatusNotifierModel(this);
    m_cpuModel = new CpuModel(this);
    m_temperatureModel = new TemperatureModel(this);
    m_networkModel = new NetworkModel(this);
    m_networkManagerModel = new NetworkManagerModel(this);
    m_brightnessModel = new BrightnessModel(this);
    m_caffeineModel = new CaffeineModel(this);
    m_soundModel = new SoundModel(this);
    m_batteryModel = new BatteryModel(this);
    connect(m_i3Ipc, SIGNAL(qbarNodeFound(qint64)), this, SLOT(handleQbarNodeFound(qint64)));
    buildLayout();
    positionAtTop();
    m_i3Ipc->start();
    m_trayModel->start();
}

void BarWindow::closeEvent(QCloseEvent *event)
{
    qWarning() << "QBar closeEvent";
    QWidget::closeEvent(event);
    QApplication::quit();
}

void BarWindow::hideEvent(QHideEvent *event)
{
    qWarning() << "QBar hideEvent";
    QWidget::hideEvent(event);
}

void BarWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void BarWindow::showEvent(QShowEvent *event)
{
    qWarning() << "QBar showEvent";
    QWidget::showEvent(event);

    if (!m_platformIntegrationApplied) {
        applyPlatformBarIntegration(this, m_config);
        scheduleTestWindowRules();
        m_platformIntegrationApplied = true;
    }
}

void BarWindow::configureWindow()
{
    setWindowTitle(QStringLiteral("QBar"));
    setProperty("appId", QStringLiteral("qbar"));
    setProperty("desktopFileName", QStringLiteral("qbar"));
    const int windowHeight = !m_config.waylandLayerShell && m_config.height < 50 ? 50 : m_config.height;
    setMinimumHeight(windowHeight);
    setMaximumHeight(windowHeight);
    Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint;
    if (!m_config.waylandLayerShell) {
        flags |= Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setStyleSheet(QStringLiteral(
                      "QFrame#CalendarPopup {"
                      "background: %1;"
                      "border: 1px solid rgba(255, 255, 255, 48);"
                      "border-radius: 2px;"
                      "}"
                      "QCalendarWidget { color: %2; }")
                      .arg(m_config.background.name(QColor::HexArgb),
                           m_config.foreground.name(QColor::HexArgb)));
}

void BarWindow::buildLayout()
{
    m_view = new QQuickWidget(this);
    m_view->setObjectName(QStringLiteral("BarView"));
    m_view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_view->setClearColor(Qt::transparent);
    m_view->setAutoFillBackground(false);
    m_view->setStyleSheet(QStringLiteral("background: transparent;"));
    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_view->setFixedHeight(m_config.height);
    m_view->setAttribute(Qt::WA_AlwaysStackOnTop, false);
    m_view->setAttribute(Qt::WA_TranslucentBackground, true);

    const QVariantMap theme = themeMap(m_config);
    if (m_view->engine()->imageProvider(QStringLiteral("themeicon")) == nullptr) {
        m_view->engine()->addImageProvider(QStringLiteral("themeicon"), new ThemeIconProvider);
    }

    m_popupService = new QBarPopupService(m_view->engine(), theme, m_i3Ipc->workspaceModel(), m_i3Ipc, m_trayModel, this);

    m_view->rootContext()->setContextProperty(QStringLiteral("theme"), theme);
    m_view->rootContext()->setContextProperty(QStringLiteral("qbarPopups"), m_popupService);
    m_view->rootContext()->setContextProperty(QStringLiteral("workspaceModel"), m_i3Ipc->workspaceModel());
    m_view->rootContext()->setContextProperty(QStringLiteral("cpuModel"), m_cpuModel);
    m_view->rootContext()->setContextProperty(QStringLiteral("temperatureModel"), m_temperatureModel);
    m_view->rootContext()->setContextProperty(QStringLiteral("networkModel"), m_networkModel);
    m_view->rootContext()->setContextProperty(QStringLiteral("networkManagerModel"), m_networkManagerModel);
    m_view->rootContext()->setContextProperty(QStringLiteral("brightnessModel"), m_brightnessModel);
    m_view->rootContext()->setContextProperty(QStringLiteral("caffeineModel"), m_caffeineModel);
    m_view->rootContext()->setContextProperty(QStringLiteral("soundModel"), m_soundModel);
    m_view->rootContext()->setContextProperty(QStringLiteral("i3Ipc"), m_i3Ipc);
    m_view->rootContext()->setContextProperty(QStringLiteral("trayModel"), m_trayModel);
    m_view->rootContext()->setContextProperty(QStringLiteral("batteryModel"), m_batteryModel);
    m_view->rootContext()->setContextProperty(QStringLiteral("appletNames"), m_config.applets);
    m_view->rootContext()->setContextProperty(QStringLiteral("leftApplets"), m_config.appletsLeft);
    m_view->rootContext()->setContextProperty(QStringLiteral("centerApplets"), m_config.appletsCenter);
    m_view->rootContext()->setContextProperty(QStringLiteral("rightApplets"), m_config.appletsRight);
    m_view->rootContext()->setContextProperty(QStringLiteral("barWindow"), this);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(m_config.margin, 0, m_config.margin, 0);
    outer->setSpacing(0);
    outer->addWidget(m_view);

    m_view->setSource(QUrl(QStringLiteral("qrc:/Bar.qml")));

    m_calendarPopup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    m_calendarPopup->setObjectName(QStringLiteral("CalendarPopup"));
    m_calendarPopup->setWindowTitle(QStringLiteral("QBar Calendar Popup"));
    m_calendarPopup->setAttribute(Qt::WA_TranslucentBackground, true);
    m_calendarPopup->setFocusPolicy(Qt::StrongFocus);

    auto *popupLayout = new QVBoxLayout(m_calendarPopup);
    popupLayout->setContentsMargins(10, 10, 10, 10);
    m_calendar = new QCalendarWidget(m_calendarPopup);
    popupLayout->addWidget(m_calendar);
}

void BarWindow::positionAtTop()
{
    const QRect target = targetBarGeometry();
    setMinimumSize(target.size());
    setMaximumSize(target.size());
    setGeometry(target);
}

QRect BarWindow::targetBarGeometry() const
{
    const auto *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return QRect(0, 0, 900, m_config.height);
    }

    const auto area = m_config.waylandLayerShell ? screen->availableGeometry() : screen->geometry();
    const int targetHeight = !m_config.waylandLayerShell && m_config.height < 50 ? 50 : m_config.height;
    const int y = m_config.position == BarPosition::Bottom
        ? area.y() + area.height() - targetHeight
        : area.y();
    const int targetX = m_config.x >= 0 ? m_config.x : area.x();
    const int targetY = m_config.y >= 0 ? m_config.y : y;
    return QRect(targetX, targetY, area.width(), targetHeight);
}

QString BarWindow::testWindowCriteria() const
{
    if (m_swayNodeId >= 0) {
        return QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
    }

    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    return QStringLiteral("%1; [app_id=\"qbar\"]; [title=\"QBar\"]").arg(pidCriteria);
}

void BarWindow::applyTestWindowRules()
{
    if (m_config.waylandLayerShell || m_i3Ipc == nullptr) {
        return;
    }

    if (m_swayNodeId >= 0) {
        const QString criteria = QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
        m_i3Ipc->runCommand(QStringLiteral("%1 floating enable; %1 sticky enable; %1 border none").arg(criteria));
        QTimer::singleShot(80, this, SLOT(moveTestWindow()));
        return;
    }

    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    const QString appCriteria = QStringLiteral("[app_id=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    m_i3Ipc->runCommand(QStringLiteral(
                            "%1 floating enable; %1 sticky enable; %1 border none; "
                            "%2 floating enable; %2 sticky enable; %2 border none; "
                            "%3 floating enable; %3 sticky enable; %3 border none")
                            .arg(pidCriteria, appCriteria, titleCriteria));
    QTimer::singleShot(80, this, SLOT(moveTestWindow()));
}

void BarWindow::moveTestWindow()
{
    if (m_config.waylandLayerShell || m_i3Ipc == nullptr) {
        return;
    }

    const QRect target = targetBarGeometry();
    qWarning() << "QBar test window target geometry:" << target;
    const int moveY = target.y();
    if (m_swayNodeId >= 0) {
        const QString criteria = QStringLiteral("[con_id=%1]").arg(m_swayNodeId);
        m_i3Ipc->runCommand(QStringLiteral("%1 resize set width %2 px height %3 px; %1 move absolute position %4 %5")
                                .arg(criteria)
                                .arg(target.width())
                                .arg(target.height())
                                .arg(target.x())
                                .arg(moveY));
        return;
    }

    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    const QString appCriteria = QStringLiteral("[app_id=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    m_i3Ipc->runCommand(QStringLiteral(
                            "%1 resize set width %2 px height %3 px; %1 move absolute position %4 %5; "
                            "%6 resize set width %2 px height %3 px; %6 move absolute position %4 %5; "
                            "%7 resize set width %2 px height %3 px; %7 move absolute position %4 %5")
                            .arg(pidCriteria)
                            .arg(target.width())
                            .arg(target.height())
                            .arg(target.x())
                            .arg(moveY)
                            .arg(appCriteria)
                            .arg(titleCriteria));
}

void BarWindow::handleQbarNodeFound(qint64 nodeId)
{
    m_swayNodeId = nodeId;
    applyTestWindowRules();
    QTimer::singleShot(80, this, SLOT(moveTestWindow()));
}

void BarWindow::installTestWindowRule()
{
    if (m_config.waylandLayerShell || m_i3Ipc == nullptr) {
        return;
    }

    const QRect target = targetBarGeometry();
    const QString pidCriteria = QStringLiteral("[pid=%1]").arg(static_cast<qlonglong>(getpid()));
    const QString appCriteria = QStringLiteral("[app_id=\"qbar\"]");
    const QString titleCriteria = QStringLiteral("[title=\"QBar\"]");
    Q_UNUSED(target);
    const QString action = QStringLiteral("floating enable, sticky enable, border none");

    m_i3Ipc->runCommand(QStringLiteral("for_window %1 %2; for_window %3 %2; for_window %4 %2")
                            .arg(pidCriteria, action, appCriteria, titleCriteria));
}

void BarWindow::scheduleTestWindowRules()
{
    if (m_config.waylandLayerShell) {
        return;
    }

    applyTestWindowRules();
    QTimer::singleShot(120, m_i3Ipc, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(150, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(350, m_i3Ipc, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(500, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(900, m_i3Ipc, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(1200, this, SLOT(applyTestWindowRules()));
    QTimer::singleShot(1500, m_i3Ipc, SLOT(requestTreeSnapshot()));
    QTimer::singleShot(1800, this, SLOT(moveTestWindow()));
}

void BarWindow::openCalendar(QObject *anchorObject)
{
    if (m_calendarPopup->isVisible()) {
        m_calendarPopup->hide();
        return;
    }

    const int popupWidth = 340;
    const int popupHeight = 280;
    const QSize popupSize(popupWidth, popupHeight);
    const int gap = 0;
    auto *anchorItem = qobject_cast<QQuickItem *>(anchorObject);
    QPoint anchorTopLeft = mapToGlobal(QPoint(0, 0));
    QPoint anchorBottomRight = mapToGlobal(QPoint(width(), height()));

    if (anchorItem != nullptr && m_view != nullptr) {
        const QPointF itemTopLeft = anchorItem->mapToScene(QPointF(0.0, 0.0));
        const QPointF itemBottomRight = anchorItem->mapToScene(QPointF(anchorItem->width(), anchorItem->height()));
        anchorTopLeft = m_view->mapToGlobal(QPoint(qRound(itemTopLeft.x()), qRound(itemTopLeft.y())));
        anchorBottomRight = m_view->mapToGlobal(QPoint(qRound(itemBottomRight.x()), qRound(itemBottomRight.y())));
    } else if (m_view != nullptr) {
        anchorTopLeft = m_view->mapToGlobal(QPoint(0, 0));
        anchorBottomRight = m_view->mapToGlobal(QPoint(m_view->width(), m_view->height()));
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

    const QPoint barTopLeft = mapToGlobal(QPoint(0, 0));
    const QPoint anchorRelativeTopLeft = anchorTopLeft - barTopLeft;
    const int anchorWidth = anchorItem != nullptr ? qRound(anchorItem->width()) : m_view != nullptr ? m_view->width() : width();
    const int anchorHeight = anchorItem != nullptr ? qRound(anchorItem->height()) : m_view != nullptr ? m_view->height() : height();
    qputenv("QBAR_LAYER_POPUP_ANCHOR_X", QByteArray::number(anchorRelativeTopLeft.x()));
    qputenv("QBAR_LAYER_POPUP_ANCHOR_Y", QByteArray::number(anchorRelativeTopLeft.y()));
    qputenv("QBAR_LAYER_POPUP_ANCHOR_WIDTH", QByteArray::number(anchorWidth));
    qputenv("QBAR_LAYER_POPUP_ANCHOR_HEIGHT", QByteArray::number(anchorHeight));
    qputenv("QBAR_LAYER_POPUP_ANCHOR_TITLE", QByteArray("QBar Calendar Popup"));

    m_calendarPopup->resize(popupSize);
    m_calendarPopup->move(x, y);
    m_calendarPopup->show();
    m_calendarPopup->raise();
    m_calendarPopup->setFocus(Qt::PopupFocusReason);
}

void BarWindow::cycleKeyboardLayout()
{
    if (m_i3Ipc != nullptr) {
        m_i3Ipc->cycleKeyboardLayout();
    }
}

void BarWindow::toggleCaffeine()
{
    if (m_caffeineModel != nullptr) {
        m_caffeineModel->toggle();
    }
}
