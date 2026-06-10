#include "applethost.h"

#include <QQmlContext>
#include <QQuickItem>
#include <QSizePolicy>
#include <QVariantMap>

namespace {

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
        {QStringLiteral("fontFamily"), config.fontFamily},
        {QStringLiteral("fontSize"), config.fontSize},
        {QStringLiteral("height"), config.height},
    };
}

} // namespace

AppletHost::AppletHost(QString appletName, const BarConfig &config, QWidget *parent)
    : QQuickWidget(parent)
    , m_appletName(std::move(appletName))
{
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setClearColor(Qt::transparent);
    setAttribute(Qt::WA_AlwaysStackOnTop, false);
    setMinimumHeight(config.height);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    rootContext()->setContextProperty(QStringLiteral("appletName"), m_appletName);
    rootContext()->setContextProperty(QStringLiteral("theme"), themeMap(config));

    connect(this, SIGNAL(statusChanged(QQuickWidget::Status)), this, SLOT(connectRootObject()));
    setSource(QUrl(appletSource(m_appletName)));
}

QString AppletHost::appletName() const
{
    return m_appletName;
}

void AppletHost::connectRootObject()
{
    if (status() != QQuickWidget::Ready || rootObject() == nullptr) {
        return;
    }

    connect(rootObject(), SIGNAL(activated()), this, SLOT(forwardActivation()));
}

void AppletHost::forwardActivation()
{
    emit activated(m_appletName);
}
