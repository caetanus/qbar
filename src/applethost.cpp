#include "applethost.h"

#include "qml/qbarpopupservice.h"

#include <QFileInfo>
#include <QIcon>
#include <QDirIterator>
#include <QPixmap>
#include <QPainter>
#include <QPropertyAnimation>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QSizePolicy>
#include <QVariantMap>
#include <algorithm>

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

        qDebug() << "[tray] ThemeIconProvider request:" << id << "size:" << side
                 << "parts:" << parts << "themePath:" << iconThemePath;

        QIcon icon;
        if (QFileInfo::exists(iconId)) {
            icon = QIcon(iconId);
            qDebug() << "[tray]   → loaded from file path";
        } else {
            icon = QIcon::fromTheme(iconId);
            if (!icon.isNull()) {
                qDebug() << "[tray]   → found in theme";
            } else {
                qDebug() << "[tray]   → NOT in theme, trying iconThemePath:" << iconThemePath;
                const QString fileName = findIconFile(iconId, iconThemePath);
                if (!fileName.isEmpty()) {
                    icon = QIcon(fileName);
                    qDebug() << "[tray]   → found via findIconFile:" << fileName;
                } else {
                    qDebug() << "[tray]   → NOT found via findIconFile";
                }
            }
        }
        if (icon.isNull()) {
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
            qDebug() << "[tray]   → FALLBACK: application-x-executable";
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

static QColor contrastColorFor(const QColor &background)
{
    const double luminance = (0.2126 * background.redF())
        + (0.7152 * background.greenF())
        + (0.0722 * background.blueF());
    return luminance < 0.5 ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1f2933"));
}

QString appletSource(const QString &name)
{
    return QStringLiteral("qrc:/applets/%1.qml").arg(name);
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
        {QStringLiteral("height"), config.height},
    };
}

} // namespace

AppletHost::AppletHost(QString appletName,
                       const BarConfig &config,
                       QObject *workspaceModel,
                       QObject *cpuModel,
                       QObject *temperatureModel,
                       QObject *networkModel,
                       QObject *networkManagerModel,
                       QObject *brightnessModel,
                       QObject *caffeineModel,
                       QObject *soundModel,
                       QObject *ipcClient,
                       QObject *trayModel,
                       QObject *batteryModel,
                       QWidget *parent)
    : QQuickWidget(parent)
    , m_appletName(std::move(appletName))
    , m_animationDuration(config.animationDuration)
    , m_animationEasing(config.animationEasing)
{
    qWarning() << "[applet] create host" << m_appletName;
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setClearColor(Qt::transparent);
    setAttribute(Qt::WA_AlwaysStackOnTop, false);
    setFixedHeight(config.height);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    const QVariantMap theme = themeMap(config);
    m_popupService = new QBarPopupService(engine(), theme, workspaceModel, ipcClient, trayModel, this);

    rootContext()->setContextProperty(QStringLiteral("appletName"), m_appletName);
    rootContext()->setContextProperty(QStringLiteral("theme"), theme);
    rootContext()->setContextProperty(QStringLiteral("qbarPopups"), m_popupService);
    if (engine()->imageProvider(QStringLiteral("themeicon")) == nullptr) {
        engine()->addImageProvider(QStringLiteral("themeicon"), new ThemeIconProvider);
    }
    if (workspaceModel != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("workspaceModel"), workspaceModel);
    }
    if (cpuModel != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("cpuModel"), cpuModel);
    }
    if (temperatureModel != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("temperatureModel"), temperatureModel);
    }
    if (networkModel != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("networkModel"), networkModel);
    }
    if (networkManagerModel != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("networkManagerModel"), networkManagerModel);
    }
    if (brightnessModel != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("brightnessModel"), brightnessModel);
    }
    if (caffeineModel != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("caffeineModel"), caffeineModel);
    }
    if (soundModel != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("soundModel"), soundModel);
    }
    if (ipcClient != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("i3Ipc"), ipcClient);
    }
    if (trayModel != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("trayModel"), trayModel);
    }
    if (batteryModel != nullptr) {
        rootContext()->setContextProperty(QStringLiteral("batteryModel"), batteryModel);
    }

    connect(this, SIGNAL(statusChanged(QQuickWidget::Status)), this, SLOT(onStatusChanged(QQuickWidget::Status)));
    connect(this, SIGNAL(statusChanged(QQuickWidget::Status)), this, SLOT(connectRootObject()));
    setSource(QUrl(appletSource(m_appletName)));
    qWarning() << "[applet] setSource" << m_appletName << appletSource(m_appletName);
}

QString AppletHost::appletName() const
{
    return m_appletName;
}

void AppletHost::onStatusChanged(QQuickWidget::Status status)
{
    qWarning() << "[applet] status" << m_appletName << status << "root:" << (rootObject() != nullptr);
    if (status == QQuickWidget::Error) {
        const auto qmlErrors = QQuickWidget::errors();
        for (const auto &error : qmlErrors) {
            qWarning() << "[applet] qml error" << m_appletName << error.toString();
        }
    }
}

void AppletHost::connectRootObject()
{
    if (status() != QQuickWidget::Ready || rootObject() == nullptr) {
        return;
    }

    qWarning() << "[applet] ready" << m_appletName << "root size" << rootObject()->property("width").toInt()
               << rootObject()->property("height").toInt();

    const QMetaObject *metaObject = rootObject()->metaObject();
    if (metaObject->indexOfSignal("activated()") >= 0) {
        connect(rootObject(), SIGNAL(activated()), this, SLOT(forwardActivation()));
    }
    if (metaObject->indexOfSignal("workspaceActivated(QString)") >= 0) {
        connect(rootObject(), SIGNAL(workspaceActivated(QString)), this, SLOT(forwardWorkspaceActivation(QString)));
    }
    if (metaObject->indexOfSignal("workspaceScrolled(int)") >= 0) {
        connect(rootObject(), SIGNAL(workspaceScrolled(int)), this, SLOT(forwardWorkspaceScroll(int)));
    }
    if (metaObject->indexOfSignal("preferredWidthUpdated(int)") >= 0) {
        connect(rootObject(), SIGNAL(preferredWidthUpdated(int)), this, SLOT(applyPreferredWidth(int)));
        applyPreferredWidth(rootObject()->property("preferredWidth").toInt());
    }
}

void AppletHost::forwardActivation()
{
    emit activated(m_appletName);
}

void AppletHost::forwardWorkspaceActivation(const QString &workspaceName)
{
    emit workspaceActivated(workspaceName);
}

void AppletHost::forwardWorkspaceScroll(int direction)
{
    emit workspaceScrolled(direction);
}

void AppletHost::applyPreferredWidth(int width)
{
    if (width < 0) {
        return;
    }

    auto *animation = new QPropertyAnimation(this, "minimumWidth", this);
    animation->setDuration(m_animationDuration);
    animation->setEasingCurve(m_animationEasing);
    animation->setStartValue(minimumWidth());
    animation->setEndValue(width);
    animation->start(QAbstractAnimation::DeleteWhenStopped);

    auto *maxAnimation = new QPropertyAnimation(this, "maximumWidth", this);
    maxAnimation->setDuration(m_animationDuration);
    maxAnimation->setEasingCurve(m_animationEasing);
    maxAnimation->setStartValue(maximumWidth());
    maxAnimation->setEndValue(width);
    maxAnimation->start(QAbstractAnimation::DeleteWhenStopped);

    emit preferredWidthChanged();
}
