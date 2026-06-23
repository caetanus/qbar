#include "test_localstorage.h"

#include "qml/localstorage.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

void LocalStorageTests::storesUpdatesAndRemovesValues()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    LocalStorage storage(directory.filePath(QStringLiteral("storage.db")));
    QTRY_VERIFY_WITH_TIMEOUT(storage.ready(), 2000);

    QSignalSpy stored(&storage, &LocalStorage::itemStored);
    QSignalSpy loaded(&storage, &LocalStorage::itemLoaded);
    QSignalSpy removed(&storage, &LocalStorage::itemRemoved);

    storage.setItem(QStringLiteral("widget.key"), QStringLiteral("value"));
    QTRY_COMPARE(stored.count(), 1);
    QVERIFY(stored.takeFirst().at(2).toBool());
    QTRY_COMPARE(storage.length(), 1);

    storage.getItem(QStringLiteral("widget.key"));
    QTRY_COMPARE(loaded.count(), 1);
    const QVariantList firstLoad = loaded.takeFirst();
    QVERIFY(firstLoad.at(3).toBool());
    QCOMPARE(firstLoad.at(2).toString(), QStringLiteral("value"));

    storage.setItem(QStringLiteral("widget.key"), QStringLiteral("updated"));
    QTRY_COMPARE(stored.count(), 1);
    QTRY_COMPARE(storage.length(), 1);
    storage.removeItem(QStringLiteral("widget.key"));
    QTRY_COMPARE(removed.count(), 1);
    QVERIFY(removed.takeFirst().at(2).toBool());
    QTRY_COMPARE(storage.length(), 0);
}

void LocalStorageTests::persistsAcrossInstances()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("storage.db"));
    {
        LocalStorage writer(path);
        QTRY_VERIFY_WITH_TIMEOUT(writer.ready(), 2000);
        QSignalSpy stored(&writer, &LocalStorage::itemStored);
        writer.setItem(QStringLiteral("bitcoin.drawings"), QStringLiteral("[{\"x\":1}]"));
        QTRY_COMPARE(stored.count(), 1);
        QVERIFY(stored.takeFirst().at(2).toBool());
    }

    LocalStorage reader(path);
    QTRY_VERIFY_WITH_TIMEOUT(reader.ready(), 2000);
    QSignalSpy loaded(&reader, &LocalStorage::itemLoaded);
    reader.getItem(QStringLiteral("bitcoin.drawings"));
    QTRY_COMPARE(loaded.count(), 1);
    const QVariantList result = loaded.takeFirst();
    QVERIFY(result.at(3).toBool());
    QCOMPARE(result.at(2).toString(), QStringLiteral("[{\"x\":1}]"));
}

void LocalStorageTests::listsAndClearsKeys()
{
    QTemporaryDir directory;
    LocalStorage storage(directory.filePath(QStringLiteral("storage.db")));
    QTRY_VERIFY_WITH_TIMEOUT(storage.ready(), 2000);
    QSignalSpy stored(&storage, &LocalStorage::itemStored);
    QSignalSpy listed(&storage, &LocalStorage::keysLoaded);
    QSignalSpy contained(&storage, &LocalStorage::containsLoaded);
    QSignalSpy cleared(&storage, &LocalStorage::storageCleared);

    storage.setItem(QStringLiteral("b"), QString());
    storage.setItem(QStringLiteral("a"), QStringLiteral("1"));
    QTRY_COMPARE(stored.count(), 2);
    storage.keys();
    QTRY_COMPARE(listed.count(), 1);
    QCOMPARE(listed.takeFirst().at(1).toStringList(),
             QStringList({QStringLiteral("a"), QStringLiteral("b")}));

    storage.contains(QStringLiteral("b"));
    QTRY_COMPARE(contained.count(), 1);
    QVERIFY(contained.takeFirst().at(2).toBool());

    storage.clear();
    QTRY_COMPARE(cleared.count(), 1);
    QVERIFY(cleared.takeFirst().at(1).toBool());
    QTRY_COMPARE(storage.length(), 0);
}

QTEST_GUILESS_MAIN(LocalStorageTests)
