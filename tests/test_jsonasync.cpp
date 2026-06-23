#include "test_jsonasync.h"

#include "qml/jsonasync.h"

#include <QSignalSpy>
#include <QJSEngine>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

void JsonAsyncTests::stringifiesAndParsesOffThread()
{
    JsonAsync json;
    const QVariantList source {
        QVariantMap {{QStringLiteral("type"), QStringLiteral("line")},
                     {QStringLiteral("points"), QVariantList {1, 2, 3}}}
    };

    QJSEngine engine;
    JsonReply *encodeReply = json.stringify(engine.toScriptValue(source));
    QSignalSpy encoded(encodeReply, &JsonReply::finished);
    QSignalSpy encodeFailed(encodeReply, &JsonReply::failed);
    QTRY_COMPARE_WITH_TIMEOUT(encoded.count(), 1, 2000);
    QCOMPARE(encodeFailed.count(), 0);
    const QString text = encoded.takeFirst().at(0).toString();
    QVERIFY(text.startsWith(QLatin1Char('[')));

    JsonReply *decodeReply = json.parse(text);
    QSignalSpy decoded(decodeReply, &JsonReply::finished);
    QSignalSpy decodeFailed(decodeReply, &JsonReply::failed);
    QTRY_COMPARE_WITH_TIMEOUT(decoded.count(), 1, 2000);
    QCOMPARE(decodeFailed.count(), 0);
    QCOMPARE(decoded.takeFirst().at(0).toList(), source);
}

QTEST_GUILESS_MAIN(JsonAsyncTests)
