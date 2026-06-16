#include "test_jsonc.h"

#include "json/jsonc.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

void JsoncTests::parsesPlainJson()
{
    const QJsonDocument doc = Jsonc::parse(QStringLiteral(R"({"a": 1, "b": [true, null, "x"], "c": 2.5})"));
    QVERIFY(doc.isObject());
    const QJsonObject obj = doc.object();
    QCOMPARE(obj.value(QStringLiteral("a")).toInt(), 1);
    QCOMPARE(obj.value(QStringLiteral("c")).toDouble(), 2.5);
    const QJsonArray arr = obj.value(QStringLiteral("b")).toArray();
    QCOMPARE(arr.size(), 3);
    QCOMPARE(arr.at(0).toBool(), true);
    QVERIFY(arr.at(1).isNull());
    QCOMPARE(arr.at(2).toString(), QStringLiteral("x"));
}

void JsoncTests::parsesLineComments()
{
    const QJsonDocument doc = Jsonc::parse(QStringLiteral(
        "{\n"
        "  // the bar height\n"
        "  \"height\": 28, // inline comment\n"
        "  \"name\": \"qbar\"\n"
        "}\n"));
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("height")).toInt(), 28);
    QCOMPARE(doc.object().value(QStringLiteral("name")).toString(), QStringLiteral("qbar"));
}

void JsoncTests::parsesBlockComments()
{
    const QJsonDocument doc = Jsonc::parse(QStringLiteral(
        "/* leading */ {\n"
        "  \"a\": /* between */ 1,\n"
        "  \"b\": 2\n"
        "} /* trailing */"));
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("a")).toInt(), 1);
    QCOMPARE(doc.object().value(QStringLiteral("b")).toInt(), 2);
}

void JsoncTests::allowsTrailingCommas()
{
    const QJsonDocument doc = Jsonc::parse(QStringLiteral(
        "{\n"
        "  \"list\": [1, 2, 3,],\n"
        "  \"obj\": { \"x\": 1, },\n"
        "}\n"));
    QVERIFY(doc.isObject());
    const QJsonObject obj = doc.object();
    QCOMPARE(obj.value(QStringLiteral("list")).toArray().size(), 3);
    QCOMPARE(obj.value(QStringLiteral("obj")).toObject().value(QStringLiteral("x")).toInt(), 1);
}

void JsoncTests::keepsCommentMarkersInsideStrings()
{
    const QJsonDocument doc = Jsonc::parse(QStringLiteral(R"({"url": "http://x//y", "blk": "a/*b*/c"})"));
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("url")).toString(), QStringLiteral("http://x//y"));
    QCOMPARE(doc.object().value(QStringLiteral("blk")).toString(), QStringLiteral("a/*b*/c"));
}

void JsoncTests::reportsErrors()
{
    QString error;
    const QJsonDocument doc = Jsonc::parse(QStringLiteral("{ \"a\": }"), &error);
    QVERIFY(doc.isNull());
    QVERIFY(!error.isEmpty());

    QString error2;
    const QJsonDocument unterminated = Jsonc::parse(QStringLiteral("{ /* nope }"), &error2);
    QVERIFY(unterminated.isNull());
    QVERIFY(!error2.isEmpty());
}

QTEST_GUILESS_MAIN(JsoncTests)
