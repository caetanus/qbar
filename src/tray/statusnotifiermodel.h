#pragma once

#include <QAbstractListModel>
#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QPointer>
#include <QStringList>

class QMenu;
class StatusNotifierWatcherAdaptor;

class StatusNotifierModel final : public QAbstractListModel, protected QDBusContext {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Row indices of items in the NeedsAttention state — the tray drawer keeps these visible
    // while collapsed. Re-emitted on any model change (see the constructor's connections).
    Q_PROPERTY(QVariantList attentionRows READ attentionRows NOTIFY attentionRowsChanged)

public:
    enum Role {
        ServiceRole = Qt::UserRole + 1,
        PathRole,
        TitleRole,
        IconNameRole,
        IconSourceRole,
        IconThemePathRole,
        OverlayIconNameRole,
        SymbolicIconSourceRole,
        OverlaySymbolicIconSourceRole,
        DesktopEntryRole,
        StatusRole,
        ItemIsMenuRole,
    };

    explicit StatusNotifierModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList registeredStatusNotifierItems() const;
    bool isStatusNotifierHostRegistered() const;
    int protocolVersion() const;
    int count() const;
    QVariantList attentionRows() const;

    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void activateAt(int row, int x, int y);
    Q_INVOKABLE void secondaryActivate(int row);
    Q_INVOKABLE void secondaryActivateAt(int row, int x, int y);
    Q_INVOKABLE void contextMenu(int row);
    Q_INVOKABLE void contextMenuAt(int row, int x, int y);
    Q_INVOKABLE void scroll(int row, int delta, const QString &orientation);

public slots:
    void start();
    void RegisterStatusNotifierItem(const QString &service);
    void RegisterStatusNotifierHost(const QString &service);

signals:
    void StatusNotifierItemRegistered(const QString &service);
    void StatusNotifierItemUnregistered(const QString &service);
    void StatusNotifierHostRegistered();
    void registeredStatusNotifierItemsChanged();
    void isStatusNotifierHostRegisteredChanged();
    void countChanged();
    void attentionRowsChanged();

private slots:
    void syncFromWatcher();
    void handleWatcherOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void handleExternalItemRegistered(const QString &service);
    void handleExternalItemUnregistered(const QString &service);
    void handleItemPropertiesChanged(const QString &interface, const QVariantMap &changedProperties, const QStringList &invalidatedProperties);
    void handleItemChanged();
    void handleItemStatusChanged(const QString &status);

private:
    struct Item {
        QString service;
        QString path;
        QString title;
        QString iconName;
        QString iconSource;
        QString iconPixmapSource;
        QString attentionIconName;
        QString attentionIconPixmapSource;
        QString overlayIconName;
        QString overlayIconPixmapSource;
        QString symbolicIconSource;
        QString overlaySymbolicIconSource;
        QString iconThemePath;
        QString desktopEntry;
        QString menuPath;
        QString status;
        bool itemIsMenu = true;
    };

    void registerOwnWatcher();
    void registerExternalHost();
    void connectExternalWatcherSignals();
    void addItemAddress(const QString &address);
    void removeItemAddress(const QString &address);
    void refreshItem(int row);
    void applyItemProperties(const QString &service, const QString &path, const QVariantMap &values);
    void callItemMethod(int row, const QString &method, int x, int y);
    bool showDbusMenu(int row, int x, int y);
    int itemRow(const QString &service, const QString &path) const;
    bool splitAddress(const QString &address, QString *service, QString *path) const;
    QString normalizeAddress(const QString &service) const;
    QString itemAddress(const Item &item) const;

    QList<Item> m_items;
    QDBusServiceWatcher m_watcher;
    QPointer<QMenu> m_openMenu;
    StatusNotifierWatcherAdaptor *m_watcherAdaptor = nullptr;
    bool m_ownsWatcher = false;
    bool m_hostRegistered = false;
};

// Exports only the org.kde.StatusNotifierWatcher interface on D-Bus, forwarding to the model.
// Registering the QAbstractListModel directly would make QtDBus walk every inherited
// QAbstractItemModel method/signal (QModelIndex, QVariant, QFlags...), which it cannot marshal —
// producing a flood of "Type not registered with QtDBus" warnings. An adaptor scopes the export.
class StatusNotifierWatcherAdaptor final : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredStatusNotifierItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ isStatusNotifierHostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)

public:
    explicit StatusNotifierWatcherAdaptor(StatusNotifierModel *model);

    QStringList registeredStatusNotifierItems() const;
    bool isStatusNotifierHostRegistered() const;
    int protocolVersion() const;

public slots:
    void RegisterStatusNotifierItem(const QString &service);
    void RegisterStatusNotifierHost(const QString &service);

signals:
    void StatusNotifierItemRegistered(const QString &service);
    void StatusNotifierItemUnregistered(const QString &service);
    void StatusNotifierHostRegistered();

private:
    StatusNotifierModel *m_model;
};
