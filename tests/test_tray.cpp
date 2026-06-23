#include "test_tray.h"

#include "src/tray/statusnotifiermodel.h"

#include <QMetaMethod>
#include <QMetaProperty>
#include <QTest>

namespace {

QMetaMethod requireMethod(const QMetaObject &metaObject, const char *signature)
{
    const int index = metaObject.indexOfMethod(signature);
    Q_ASSERT(index >= 0);
    return metaObject.method(index);
}

QMetaProperty requireProperty(const QMetaObject &metaObject, const char *name)
{
    const int index = metaObject.indexOfProperty(name);
    Q_ASSERT(index >= 0);
    return metaObject.property(index);
}

bool isScriptable(const QMetaMethod &method)
{
    return (method.attributes() & QMetaMethod::Scriptable) != 0;
}

} // namespace

void TrayTests::watcherExportsOnlyDbusSurface()
{
    const QMetaObject &metaObject = StatusNotifierModel::staticMetaObject;

    QVERIFY(isScriptable(requireMethod(metaObject, "RegisterStatusNotifierItem(QString)")));
    QVERIFY(isScriptable(requireMethod(metaObject, "RegisterStatusNotifierHost(QString)")));
    QVERIFY(!isScriptable(requireMethod(metaObject, "start()")));

    QVERIFY(isScriptable(requireMethod(metaObject, "StatusNotifierItemRegistered(QString)")));
    QVERIFY(isScriptable(requireMethod(metaObject, "StatusNotifierItemUnregistered(QString)")));
    QVERIFY(isScriptable(requireMethod(metaObject, "StatusNotifierHostRegistered()")));
    QVERIFY(!isScriptable(requireMethod(metaObject, "registeredStatusNotifierItemsChanged()")));
    QVERIFY(!isScriptable(requireMethod(metaObject, "countChanged()")));
    QVERIFY(!isScriptable(requireMethod(metaObject, "rowsInserted(QModelIndex,int,int)")));

    QVERIFY(requireProperty(metaObject, "RegisteredStatusNotifierItems").isScriptable());
    QVERIFY(requireProperty(metaObject, "IsStatusNotifierHostRegistered").isScriptable());
    QVERIFY(requireProperty(metaObject, "ProtocolVersion").isScriptable());
    QVERIFY(!requireProperty(metaObject, "count").isScriptable());
    QVERIFY(!requireProperty(metaObject, "attentionRows").isScriptable());
}

QTEST_GUILESS_MAIN(TrayTests)
