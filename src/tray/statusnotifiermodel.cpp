#include "statusnotifiermodel.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QBuffer>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDirIterator>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QIODevice>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>
#include <cstring>
#include <limits>

namespace {

struct TrayImage {
    int width = 0;
    int height = 0;
    QByteArray data;
};

using TrayImageVector = QList<TrayImage>;

constexpr auto watcherService = "org.kde.StatusNotifierWatcher";
constexpr auto watcherPath = "/StatusNotifierWatcher";
constexpr auto watcherInterface = "org.kde.StatusNotifierWatcher";
constexpr auto itemInterface = "org.kde.StatusNotifierItem";
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto dbusMenuInterface = "com.canonical.dbusmenu";

struct DbusMenuItem {
    int id = 0;
    QVariantMap properties;
    QList<DbusMenuItem> children;
};

QVariant propertyValue(const QVariantMap &properties, const QString &key)
{
    const QVariant value = properties.value(key);
    if (value.canConvert<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant();
    }

    return value;
}

QString objectPathValue(const QVariant &value)
{
    if (value.canConvert<QDBusObjectPath>()) {
        return value.value<QDBusObjectPath>().path();
    }

    return value.toString();
}

QVariant menuProperty(const DbusMenuItem &item, const QString &key, const QVariant &fallback = {})
{
    const QVariant value = propertyValue(item.properties, key);
    return value.isValid() ? value : fallback;
}

bool menuBoolProperty(const DbusMenuItem &item, const QString &key, bool fallback)
{
    const QVariant value = menuProperty(item, key);
    return value.isValid() ? value.toBool() : fallback;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, TrayImage &image)
{
    argument.beginStructure();
    argument >> image.width >> image.height >> image.data;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DbusMenuItem &item)
{
    argument.beginStructure();
    argument >> item.id >> item.properties;
    argument.beginArray();
    while (!argument.atEnd()) {
        QDBusVariant childVariant;
        argument >> childVariant;
        const QDBusArgument childArgument = qvariant_cast<QDBusArgument>(childVariant.variant());
        DbusMenuItem child;
        childArgument >> child;
        item.children.append(child);
    }
    argument.endArray();
    argument.endStructure();
    return argument;
}

uint menuTimestamp()
{
    return static_cast<uint>(QDateTime::currentMSecsSinceEpoch() & 0xffffffff);
}

void sendDbusMenuEvent(const QString &service, const QString &path, int id, const QString &eventId)
{
    QDBusInterface iface(service, path, QString::fromLatin1(dbusMenuInterface), QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.asyncCallWithArgumentList(
            QStringLiteral("Event"),
            {id, eventId, QVariant::fromValue(QDBusVariant(QVariant(QString()))), menuTimestamp()});
    }
}

QString dbusMenuLabel(const DbusMenuItem &item)
{
    QString label = menuProperty(item, QStringLiteral("label")).toString();
    label.replace(QLatin1Char('&'), QStringLiteral("&&"));

    QString converted;
    converted.reserve(label.size());
    for (qsizetype i = 0; i < label.size(); ++i) {
        if (label.at(i) == QLatin1Char('_')) {
            if (i + 1 < label.size() && label.at(i + 1) == QLatin1Char('_')) {
                converted.append(QLatin1Char('_'));
                ++i;
            } else {
                converted.append(QLatin1Char('&'));
            }
        } else {
            converted.append(label.at(i));
        }
    }

    return converted.isEmpty() ? QStringLiteral(" ") : converted;
}

void populateDbusMenu(QMenu *menu, const QString &service, const QString &path, const QList<DbusMenuItem> &items)
{
    for (const DbusMenuItem &item : items) {
        const bool visible = menuBoolProperty(item, QStringLiteral("visible"), true);
        if (!visible) {
            continue;
        }

        const QString type = menuProperty(item, QStringLiteral("type")).toString();
        if (type == QStringLiteral("separator")) {
            menu->addSeparator();
            continue;
        }

        const QString label = dbusMenuLabel(item);

        QMenu *targetMenu = menu;
        QAction *action = nullptr;
        if (!item.children.isEmpty()) {
            QMenu *subMenu = menu->addMenu(label);
            targetMenu = subMenu;
            action = subMenu->menuAction();
            QObject::connect(subMenu, &QMenu::aboutToShow, subMenu, [service, path, id = item.id]() {
                sendDbusMenuEvent(service, path, id, QStringLiteral("opened"));
            });
            QObject::connect(subMenu, &QMenu::aboutToHide, subMenu, [service, path, id = item.id]() {
                sendDbusMenuEvent(service, path, id, QStringLiteral("closed"));
            });
            populateDbusMenu(subMenu, service, path, item.children);
        } else {
            action = menu->addAction(label);
        }

        const QString iconName = menuProperty(item, QStringLiteral("icon-name")).toString();
        if (!iconName.isEmpty()) {
            action->setIcon(QIcon::fromTheme(iconName));
        }

        action->setEnabled(menuBoolProperty(item, QStringLiteral("enabled"), true));

        const QString toggleType = menuProperty(item, QStringLiteral("toggle-type")).toString();
        if (!toggleType.isEmpty()) {
            action->setCheckable(true);
            action->setChecked(menuProperty(item, QStringLiteral("toggle-state")).toInt() == 1);
        }

        if (targetMenu == menu) {
            QObject::connect(action, &QAction::triggered, menu, [service, path, id = item.id]() {
                sendDbusMenuEvent(service, path, id, QStringLiteral("clicked"));
            });
        }
    }
}

QString resolveDesktopIcon(const QString &desktopEntry)
{
    if (desktopEntry.isEmpty()) {
        return {};
    }

    const QString name = desktopEntry.endsWith(QStringLiteral(".desktop"))
        ? desktopEntry
        : desktopEntry + QStringLiteral(".desktop");

    const QStringList dirs = QStandardPaths::locateAll(
        QStandardPaths::ApplicationsLocation, name, QStandardPaths::LocateFile);

    qDebug() << "[tray] resolveDesktopIcon:" << desktopEntry << "→ searching for" << name;
    qDebug() << "[tray]   found" << dirs.size() << "desktop file(s):" << dirs;

    for (const QString &path : dirs) {
        QSettings desktop(path, QSettings::IniFormat);
        desktop.beginGroup(QStringLiteral("Desktop Entry"));
        const QString icon = desktop.value(QStringLiteral("Icon")).toString();
        desktop.endGroup();

        qDebug() << "[tray]   desktop file:" << path << "Icon=" << icon;

        if (!icon.isEmpty() && !QIcon::fromTheme(icon).isNull()) {
            qDebug() << "[tray]   → theme resolved, using:" << icon;
            return icon;
        }
        qDebug() << "[tray]   → theme could NOT resolve:" << icon;
    }

    qDebug() << "[tray]   → no desktop icon found via theme";
    return {};
}

QString resolveIconPath(const QString &name, const QString &iconThemePath)
{
    if (name.isEmpty()) {
        return {};
    }

    if (QFileInfo::exists(name)) {
        qDebug() << "[tray] resolveIconPath:" << name << "→ file exists";
        return name;
    }

    if (!QIcon::fromTheme(name).isNull()) {
        qDebug() << "[tray] resolveIconPath:" << name << "→ found in theme";
        return name;
    }

    if (!iconThemePath.isEmpty()) {
        const QStringList names = {
            name + QStringLiteral(".png"),
            name + QStringLiteral(".svg"),
            name + QStringLiteral(".xpm"),
        };
        QDirIterator it(iconThemePath, names, QDir::Files, QDirIterator::Subdirectories);
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
        if (!best.isEmpty()) {
            qDebug() << "[tray] resolveIconPath:" << name << "→ found in iconThemePath:" << best << "size:" << bestSize;
            return best;
        }
    }

    qDebug() << "[tray] resolveIconPath:" << name << "→ NOT found (themePath:" << iconThemePath << ")";
    return {};
}

QImage imageFromTrayImageVector(const TrayImageVector &images)
{
    if (images.isEmpty()) {
        return {};
    }

    QImage bestImage;
    int bestArea = 0;
    for (const TrayImage &icon : images) {
        if (icon.width <= 0 || icon.height <= 0 || icon.data.isEmpty()) {
            continue;
        }

        const int area = icon.width * icon.height;
        if (area <= bestArea) {
            continue;
        }

        const int rowBytes = icon.width * 4;
        const int expectedSize = rowBytes * icon.height;
        if (icon.data.size() < expectedSize) {
            continue;
        }

        QImage image(icon.width, icon.height, QImage::Format_RGBA8888);
        if (image.isNull()) {
            continue;
        }

        for (int y = 0; y < icon.height; ++y) {
            const uchar *src = reinterpret_cast<const uchar *>(icon.data.constData()) + y * rowBytes;
            uchar *dst = image.scanLine(y);
            for (int x = 0; x < icon.width; ++x) {
                const int i = x * 4;
                const uchar alpha = src[i];
                dst[i + 0] = src[i + 1]; // R
                dst[i + 1] = src[i + 2]; // G
                dst[i + 2] = src[i + 3]; // B
                dst[i + 3] = alpha;      // A
            }
        }

        bestImage = image;
        bestArea = area;
    }

    return bestImage;
}

QImage imageFromXdgImageVariant(const QVariant &value)
{
    if (!value.isValid()) {
        return {};
    }

    const QDBusArgument argument = qvariant_cast<QDBusArgument>(value);
    const TrayImageVector images = qdbus_cast<TrayImageVector>(argument);
    return imageFromTrayImageVector(images);
}

QString imageDataUrl(const QVariant &value)
{
    const QImage image = imageFromXdgImageVariant(value);
    if (image.isNull()) {
        return {};
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        return {};
    }

    return QStringLiteral("data:image/png;base64,") + bytes.toBase64();
}

} // namespace

StatusNotifierModel::StatusNotifierModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_watcher.setConnection(QDBusConnection::sessionBus());
    m_watcher.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    m_watcher.addWatchedService(QString::fromLatin1(watcherService));
    connect(&m_watcher,
            SIGNAL(serviceOwnerChanged(QString,QString,QString)),
            this,
            SLOT(handleWatcherOwnerChanged(QString,QString,QString)));
}

int StatusNotifierModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant StatusNotifierModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const Item &item = m_items.at(index.row());
    switch (role) {
    case ServiceRole:
        return item.service;
    case PathRole:
        return item.path;
    case TitleRole:
        return item.title;
    case IconNameRole:
        return item.iconName;
    case IconSourceRole:
        return item.iconSource;
    case IconThemePathRole:
        return item.iconThemePath;
    case OverlayIconNameRole:
        return item.overlayIconName;
    case DesktopEntryRole:
        return item.desktopEntry;
    case StatusRole:
        return item.status;
    case ItemIsMenuRole:
        return item.itemIsMenu;
    default:
        return {};
    }
}

QHash<int, QByteArray> StatusNotifierModel::roleNames() const
{
    return {
        {ServiceRole, "service"},
        {PathRole, "path"},
        {TitleRole, "title"},
        {IconNameRole, "iconName"},
        {IconSourceRole, "iconSource"},
        {IconThemePathRole, "iconThemePath"},
        {OverlayIconNameRole, "overlayIconName"},
        {DesktopEntryRole, "desktopEntry"},
        {StatusRole, "status"},
        {ItemIsMenuRole, "itemIsMenu"},
    };
}

QStringList StatusNotifierModel::registeredStatusNotifierItems() const
{
    QStringList items;
    items.reserve(m_items.size());
    for (const Item &item : m_items) {
        items.append(itemAddress(item));
    }
    return items;
}

bool StatusNotifierModel::isStatusNotifierHostRegistered() const
{
    return m_hostRegistered || m_ownsWatcher;
}

int StatusNotifierModel::protocolVersion() const
{
    return 0;
}

int StatusNotifierModel::count() const
{
    return m_items.size();
}

void StatusNotifierModel::activate(int row)
{
    const QPoint position = QCursor::pos();
    activateAt(row, position.x(), position.y());
}

void StatusNotifierModel::activateAt(int row, int x, int y)
{
    if (row < 0 || row >= m_items.size()) {
        return;
    }
    if (x == 0 && y == 0) {
        const QPoint position = QCursor::pos();
        x = position.x();
        y = position.y();
    }

    const Item &item = m_items.at(row);
    if (item.itemIsMenu) {
        contextMenuAt(row, x, y);
        return;
    }

    callItemMethod(row, QStringLiteral("Activate"), x, y);
}

void StatusNotifierModel::secondaryActivate(int row)
{
    const QPoint position = QCursor::pos();
    secondaryActivateAt(row, position.x(), position.y());
}

void StatusNotifierModel::secondaryActivateAt(int row, int x, int y)
{
    if (row < 0 || row >= m_items.size()) {
        return;
    }

    callItemMethod(row, QStringLiteral("SecondaryActivate"), x, y);
}

void StatusNotifierModel::contextMenu(int row)
{
    const QPoint position = QCursor::pos();
    contextMenuAt(row, position.x(), position.y());
}

void StatusNotifierModel::contextMenuAt(int row, int x, int y)
{
    if (row < 0 || row >= m_items.size()) {
        return;
    }
    if (x == 0 && y == 0) {
        const QPoint position = QCursor::pos();
        x = position.x();
        y = position.y();
    }

    if (showDbusMenu(row, x, y)) {
        return;
    }

    callItemMethod(row, QStringLiteral("ContextMenu"), x, y);
}

void StatusNotifierModel::scroll(int row, int delta, const QString &orientation)
{
    if (row < 0 || row >= m_items.size() || delta == 0) {
        return;
    }

    const Item &item = m_items.at(row);
    QDBusInterface iface(item.service, item.path, QString::fromLatin1(itemInterface), QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.asyncCall(QStringLiteral("Scroll"), delta, orientation);
    }
}

void StatusNotifierModel::start()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    if (bus != nullptr && bus->isServiceRegistered(QString::fromLatin1(watcherService))) {
        syncFromWatcher();
        connectExternalWatcherSignals();
        registerExternalHost();
        return;
    }

    registerOwnWatcher();
}

void StatusNotifierModel::RegisterStatusNotifierItem(const QString &service)
{
    const QString address = normalizeAddress(service);
    if (address.isEmpty()) {
        return;
    }

    addItemAddress(address);
    emit StatusNotifierItemRegistered(address);
}

void StatusNotifierModel::RegisterStatusNotifierHost(const QString &)
{
    if (m_hostRegistered) {
        return;
    }

    m_hostRegistered = true;
    emit isStatusNotifierHostRegisteredChanged();
    emit StatusNotifierHostRegistered();
}

void StatusNotifierModel::syncFromWatcher()
{
    QDBusInterface watcher(QString::fromLatin1(watcherService),
                           QString::fromLatin1(watcherPath),
                           QString::fromLatin1(propertiesInterface),
                           QDBusConnection::sessionBus());
    const QDBusReply<QVariant> reply = watcher.call(QStringLiteral("Get"),
                                                    QString::fromLatin1(watcherInterface),
                                                    QStringLiteral("RegisteredStatusNotifierItems"));
    if (!reply.isValid()) {
        return;
    }

    const QStringList addresses = reply.value().toStringList();
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
    for (const QString &address : addresses) {
        addItemAddress(address);
    }
}

void StatusNotifierModel::handleWatcherOwnerChanged(const QString &, const QString &, const QString &newOwner)
{
    if (newOwner.isEmpty()) {
        registerOwnWatcher();
        return;
    }

    if (!m_ownsWatcher) {
        syncFromWatcher();
        connectExternalWatcherSignals();
        registerExternalHost();
    }
}

void StatusNotifierModel::handleExternalItemRegistered(const QString &service)
{
    addItemAddress(service);
}

void StatusNotifierModel::handleExternalItemUnregistered(const QString &service)
{
    removeItemAddress(service);
}

void StatusNotifierModel::registerOwnWatcher()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QString::fromLatin1(watcherService))) {
        return;
    }

    m_ownsWatcher = bus.registerObject(QString::fromLatin1(watcherPath),
                                       this,
                                       QDBusConnection::ExportAllSlots
                                           | QDBusConnection::ExportAllSignals
                                           | QDBusConnection::ExportAllProperties);
    if (!m_ownsWatcher) {
        bus.unregisterService(QString::fromLatin1(watcherService));
        return;
    }

    m_hostRegistered = true;
    emit isStatusNotifierHostRegisteredChanged();
    emit StatusNotifierHostRegistered();
}

void StatusNotifierModel::connectExternalWatcherSignals()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QString::fromLatin1(watcherService),
                QString::fromLatin1(watcherPath),
                QString::fromLatin1(watcherInterface),
                QStringLiteral("StatusNotifierItemRegistered"),
                this,
                SLOT(handleExternalItemRegistered(QString)));
    bus.connect(QString::fromLatin1(watcherService),
                QString::fromLatin1(watcherPath),
                QString::fromLatin1(watcherInterface),
                QStringLiteral("StatusNotifierItemUnregistered"),
                this,
                SLOT(handleExternalItemUnregistered(QString)));
}

void StatusNotifierModel::registerExternalHost()
{
    QDBusInterface watcher(QString::fromLatin1(watcherService),
                           QString::fromLatin1(watcherPath),
                           QString::fromLatin1(watcherInterface),
                           QDBusConnection::sessionBus());
    if (!watcher.isValid()) {
        return;
    }

    watcher.asyncCall(QStringLiteral("RegisterStatusNotifierHost"), QStringLiteral("qbar"));
    if (!m_hostRegistered) {
        m_hostRegistered = true;
        emit isStatusNotifierHostRegisteredChanged();
    }
}

void StatusNotifierModel::addItemAddress(const QString &address)
{
    QString service;
    QString path;
    if (!splitAddress(normalizeAddress(address), &service, &path)) {
        return;
    }

    Item item;
    item.service = service;
    item.path = path;
    if (itemRow(item.service, item.path) >= 0) {
        refreshItem(itemRow(item.service, item.path));
        return;
    }

    const int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.append(item);
    endInsertRows();

    QDBusConnection::sessionBus().connect(item.service,
                                          item.path,
                                          QString::fromLatin1(propertiesInterface),
                                          QStringLiteral("PropertiesChanged"),
                                          this,
                                          SLOT(handleItemPropertiesChanged(QString,QVariantMap,QStringList)));
    QDBusConnection::sessionBus().connect(item.service,
                                          item.path,
                                          QString::fromLatin1(itemInterface),
                                          QStringLiteral("NewIcon"),
                                          this,
                                          SLOT(handleItemChanged()));
    QDBusConnection::sessionBus().connect(item.service,
                                          item.path,
                                          QString::fromLatin1(itemInterface),
                                          QStringLiteral("NewTitle"),
                                          this,
                                          SLOT(handleItemChanged()));
    QDBusConnection::sessionBus().connect(item.service,
                                          item.path,
                                          QString::fromLatin1(itemInterface),
                                          QStringLiteral("NewToolTip"),
                                          this,
                                          SLOT(handleItemChanged()));
    QDBusConnection::sessionBus().connect(item.service,
                                          item.path,
                                          QString::fromLatin1(itemInterface),
                                          QStringLiteral("NewAttentionIcon"),
                                          this,
                                          SLOT(handleItemChanged()));
    QDBusConnection::sessionBus().connect(item.service,
                                          item.path,
                                          QString::fromLatin1(itemInterface),
                                          QStringLiteral("NewOverlayIcon"),
                                          this,
                                          SLOT(handleItemChanged()));
    QDBusConnection::sessionBus().connect(item.service,
                                          item.path,
                                          QString::fromLatin1(itemInterface),
                                          QStringLiteral("NewStatus"),
                                          this,
                                          SLOT(handleItemStatusChanged(QString)));
    refreshItem(row);
    emit registeredStatusNotifierItemsChanged();
    emit countChanged();
}

void StatusNotifierModel::removeItemAddress(const QString &address)
{
    QString service;
    QString path;
    if (!splitAddress(normalizeAddress(address), &service, &path)) {
        return;
    }

    for (int row = 0; row < m_items.size(); ++row) {
        const Item &item = m_items.at(row);
        if (item.service == service && item.path == path) {
            QDBusConnection::sessionBus().disconnect(item.service,
                                                     item.path,
                                                     QString::fromLatin1(propertiesInterface),
                                                     QStringLiteral("PropertiesChanged"),
                                                     this,
                                                     SLOT(handleItemPropertiesChanged(QString,QVariantMap,QStringList)));
            QDBusConnection::sessionBus().disconnect(item.service,
                                                     item.path,
                                                     QString::fromLatin1(itemInterface),
                                                     QString(),
                                                     this,
                                                     nullptr);
            beginRemoveRows(QModelIndex(), row, row);
            m_items.removeAt(row);
            endRemoveRows();
            emit registeredStatusNotifierItemsChanged();
            emit StatusNotifierItemUnregistered(address);
            emit countChanged();
            return;
        }
    }
}

void StatusNotifierModel::handleItemPropertiesChanged(const QString &interface,
                                                      const QVariantMap &,
                                                      const QStringList &)
{
    if (interface != QString::fromLatin1(itemInterface)) {
        return;
    }

    const QDBusMessage message = QDBusContext::message();
    const int row = itemRow(message.service(), message.path());
    if (row >= 0) {
        refreshItem(row);
    }
}

void StatusNotifierModel::handleItemChanged()
{
    const QDBusMessage message = QDBusContext::message();
    const int row = itemRow(message.service(), message.path());
    if (row >= 0) {
        refreshItem(row);
    }
}

void StatusNotifierModel::handleItemStatusChanged(const QString &status)
{
    const QDBusMessage message = QDBusContext::message();
    const int row = itemRow(message.service(), message.path());
    if (row < 0) {
        return;
    }

    m_items[row].status = status;
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, {StatusRole});
}

void StatusNotifierModel::refreshItem(int row)
{
    if (row < 0 || row >= m_items.size()) {
        return;
    }

    Item &item = m_items[row];
    QDBusInterface properties(item.service,
                              item.path,
                              QString::fromLatin1(propertiesInterface),
                              QDBusConnection::sessionBus());
    const QDBusReply<QVariantMap> reply = properties.call(QStringLiteral("GetAll"), QString::fromLatin1(itemInterface));
    if (reply.isValid()) {
        const QVariantMap values = reply.value();
        item.title = propertyValue(values, QStringLiteral("Title")).toString();
        item.iconName = propertyValue(values, QStringLiteral("IconName")).toString();
        item.iconPixmapSource = imageDataUrl(propertyValue(values, QStringLiteral("IconPixmap")));
        item.attentionIconName = propertyValue(values, QStringLiteral("AttentionIconName")).toString();
        item.attentionIconPixmapSource = imageDataUrl(propertyValue(values, QStringLiteral("AttentionIconPixmap")));
        item.overlayIconName = propertyValue(values, QStringLiteral("OverlayIconName")).toString();
        item.overlayIconPixmapSource = imageDataUrl(propertyValue(values, QStringLiteral("OverlayIconPixmap")));
        item.iconThemePath = propertyValue(values, QStringLiteral("IconThemePath")).toString();
        item.desktopEntry = propertyValue(values, QStringLiteral("DesktopEntry")).toString();
        item.menuPath = objectPathValue(propertyValue(values, QStringLiteral("Menu")));
        if (values.contains(QStringLiteral("ItemIsMenu"))) {
            item.itemIsMenu = propertyValue(values, QStringLiteral("ItemIsMenu")).toBool();
        }
        item.status = propertyValue(values, QStringLiteral("Status")).toString();
    }

    const QString desktopIcon = resolveDesktopIcon(item.desktopEntry);

    qDebug() << "[tray] refreshItem:" << item.service << "desktopEntry:" << item.desktopEntry
             << "iconName:" << item.iconName << "iconThemePath:" << item.iconThemePath
             << "hasPixmap:" << !item.iconPixmapSource.isEmpty();

    const auto iconUrl = [&](const QString &name) -> QString {
        const QString resolved = resolveIconPath(name, item.iconThemePath);
        if (resolved.isEmpty()) {
            return {};
        }
        return QStringLiteral("image://themeicon/") + resolved;
    };

    item.iconSource.clear();
    if (item.status == QStringLiteral("NeedsAttention")) {
        item.iconSource = iconUrl(item.attentionIconName);
        if (item.iconSource.isEmpty() && !item.attentionIconPixmapSource.isEmpty()) {
            item.iconSource = item.attentionIconPixmapSource;
        }
    }
    if (item.iconSource.isEmpty()) {
        if (!desktopIcon.isEmpty()) {
            item.iconSource = iconUrl(desktopIcon);
            qDebug() << "[tray]   → using desktop icon:" << desktopIcon;
        }
        if (item.iconSource.isEmpty()) {
            item.iconSource = iconUrl(item.iconName);
            if (!item.iconSource.isEmpty()) {
                qDebug() << "[tray]   → using SNI iconName:" << item.iconName;
            }
        }
        if (item.iconSource.isEmpty() && !item.iconPixmapSource.isEmpty()) {
            item.iconSource = item.iconPixmapSource;
            qDebug() << "[tray]   → using pixmap data URL";
        }
        if (item.iconSource.isEmpty()) {
            qDebug() << "[tray]   → NO icon source found!";
        }
    }

    if (item.title.isEmpty()) {
        item.title = item.service;
    }

    const QModelIndex changed = index(row);
    emit dataChanged(changed,
                     changed,
                     {TitleRole,
                      IconNameRole,
                      IconSourceRole,
                      IconThemePathRole,
                      OverlayIconNameRole,
                      DesktopEntryRole,
                      StatusRole,
                      ItemIsMenuRole});
}

void StatusNotifierModel::callItemMethod(int row, const QString &method, int x, int y)
{
    if (row < 0 || row >= m_items.size()) {
        return;
    }

    const Item &item = m_items.at(row);
    QDBusInterface iface(item.service, item.path, QString::fromLatin1(itemInterface), QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.asyncCall(method, x, y);
    }
}

bool StatusNotifierModel::showDbusMenu(int row, int x, int y)
{
    if (row < 0 || row >= m_items.size()) {
        return false;
    }

    const Item &item = m_items.at(row);
    if (item.menuPath.isEmpty()) {
        return false;
    }

    QDBusInterface iface(item.service, item.menuPath, QString::fromLatin1(dbusMenuInterface), QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        return false;
    }

    iface.call(QStringLiteral("AboutToShow"), 0);
    const QDBusMessage reply = iface.call(QStringLiteral("GetLayout"), 0, -1, QStringList());
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().size() < 2) {
        qWarning() << "[tray] DBusMenu GetLayout failed:" << item.service << item.menuPath << reply.errorMessage();
        return false;
    }

    const QDBusArgument layoutArgument = qvariant_cast<QDBusArgument>(reply.arguments().at(1));
    DbusMenuItem root;
    layoutArgument >> root;

    if (root.children.isEmpty()) {
        return false;
    }

    if (m_openMenu != nullptr) {
        m_openMenu->close();
        m_openMenu->deleteLater();
    }

    auto *menu = new QMenu(qobject_cast<QWidget *>(QApplication::activeWindow()));
    m_openMenu = menu;
    connect(menu, &QMenu::aboutToShow, menu, [service = item.service, path = item.menuPath, id = root.id]() {
        sendDbusMenuEvent(service, path, id, QStringLiteral("opened"));
    });
    connect(menu, &QMenu::aboutToHide, menu, [service = item.service, path = item.menuPath, id = root.id]() {
        sendDbusMenuEvent(service, path, id, QStringLiteral("closed"));
    });
    connect(menu, &QMenu::aboutToHide, menu, &QMenu::deleteLater);
    connect(menu, &QObject::destroyed, this, [this, menu]() {
        if (m_openMenu == menu) {
            m_openMenu = nullptr;
        }
    });

    populateDbusMenu(menu, item.service, item.menuPath, root.children);
    if (menu->actions().isEmpty()) {
        menu->deleteLater();
        return false;
    }

    menu->popup(QPoint(x, y));
    return true;
}

int StatusNotifierModel::itemRow(const QString &service, const QString &path) const
{
    for (int row = 0; row < m_items.size(); ++row) {
        const Item &item = m_items.at(row);
        if (item.service == service && item.path == path) {
            return row;
        }
    }

    return -1;
}

bool StatusNotifierModel::splitAddress(const QString &address, QString *service, QString *path) const
{
    const int pathStart = address.indexOf(QLatin1Char('/'));
    if (pathStart <= 0) {
        return false;
    }

    const QString parsedService = address.left(pathStart);
    const QString parsedPath = address.mid(pathStart);
    if (parsedService.isEmpty() || parsedPath.isEmpty()) {
        return false;
    }

    if (service != nullptr) {
        *service = parsedService;
    }
    if (path != nullptr) {
        *path = parsedPath;
    }
    return true;
}

QString StatusNotifierModel::normalizeAddress(const QString &service) const
{
    if (service.isEmpty()) {
        return {};
    }

    if (service.startsWith(QLatin1Char('/'))) {
        const QDBusMessage message = QDBusContext::message();
        if (message.service().isEmpty()) {
            return {};
        }
        return message.service() + service;
    }

    if (service.contains(QLatin1Char('/'))) {
        return service;
    }

    return service + QStringLiteral("/StatusNotifierItem");
}

QString StatusNotifierModel::itemAddress(const Item &item) const
{
    return item.service + item.path;
}
