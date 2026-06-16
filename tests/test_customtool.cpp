#include "test_customtool.h"

#include "src/customtool/customtoolmodel.h"

#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

namespace {

// Writes `output` to a temp file and runs "cat <file>" through the model so
// finishWithOutput()/recomputeDisplayText() execute via the real QProcess pipeline.
void runScript(CustomToolModel &model, QTemporaryFile &script, const QByteArray &output)
{
    QVERIFY(script.open());
    script.write(output);
    script.close();

    QSignalSpy spy(&model, &CustomToolModel::displayTextChanged);
    model.setCommand(QStringLiteral("cat ") + script.fileName());
    model.refresh();
    QVERIFY(spy.wait(5000));
}

} // namespace

void CustomToolModelTests::appliesFormatPlaceholders()
{
    CustomToolModel model;
    model.setWaybarFormat(true);
    model.setFormat(QStringLiteral("{} ({percentage}%)"));

    QTemporaryFile script;
    runScript(model, script, QByteArrayLiteral(R"({"text": "R$ 5.43", "percentage": 3.05})"));

    QCOMPARE(model.text(), QStringLiteral("R$ 5.43"));
    QCOMPARE(model.displayText(), QStringLiteral("R$ 5.43 (3.05%)"));
}

void CustomToolModelTests::resolvesFormatIconFromAlt()
{
    CustomToolModel model;
    model.setWaybarFormat(true);
    model.setFormat(QStringLiteral("{icon} {} ({percentage}%)"));
    model.setFormatIcons(QVariantMap{
        {QStringLiteral("up"), QStringLiteral("▲")},
        {QStringLiteral("down"), QStringLiteral("▼")},
    });

    QTemporaryFile script;
    runScript(model, script, QByteArrayLiteral(R"({"text": "5.43", "alt": "up", "percentage": 3.05})"));

    QCOMPARE(model.displayText(), QStringLiteral("▲ 5.43 (3.05%)"));
}

void CustomToolModelTests::convertsPangoMarkupToRichText()
{
    CustomToolModel model;
    model.setWaybarFormat(true);
    model.setFormat(QStringLiteral("{}"));

    QTemporaryFile script;
    runScript(model, script, QByteArrayLiteral(R"({"text": "<span color=\"red\">hot</span>"})"));

    QCOMPARE(model.displayText(), QStringLiteral(R"(<font color="red">hot</font>)"));
}

void CustomToolModelTests::leavesEscapedAndInvalidPlaceholdersUntouched()
{
    CustomToolModel model;
    model.setWaybarFormat(true);
    model.setFormat(QStringLiteral(R"(\{text} { foo} {})"));

    QTemporaryFile script;
    runScript(model, script, QByteArrayLiteral(R"({"text": "ok"})"));

    QCOMPARE(model.displayText(), QStringLiteral(R"(\{text} { foo} ok)"));
}

void CustomToolModelTests::fallsBackToPlainTextWhenOutputIsNotJson()
{
    CustomToolModel model;
    model.setWaybarFormat(true);
    model.setFormat(QStringLiteral("[{}]"));

    QTemporaryFile script;
    runScript(model, script, QByteArrayLiteral("plain text\n"));

    QCOMPARE(model.text(), QStringLiteral("plain text"));
    QCOMPARE(model.displayText(), QStringLiteral("[plain text]"));
}

QTEST_GUILESS_MAIN(CustomToolModelTests)
