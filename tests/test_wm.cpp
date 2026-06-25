#include "test_wm.h"

#include "src/wm/bspwmbackend.h"
#include "src/wm/hyprlandbackend.h"
#include "src/wm/nullbackend.h"
#include "src/wm/windowmodel.h"
#include "src/wm/workspacemodel.h"
#include "src/wm/wmbackendfactory.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <memory>

void WindowManagerTests::workspaceModelUpdatesRoles()
{
    WorkspaceModel model;
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

    model.replace({
        {.name = QStringLiteral("1"), .output = QStringLiteral("HDMI-A-1"), .number = 1, .focused = true, .visible = true},
        {.name = QStringLiteral("2"), .output = QStringLiteral("HDMI-A-1"), .number = 2},
    });

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), WorkspaceModel::NameRole).toString(), QStringLiteral("1"));
    QCOMPARE(model.data(model.index(0, 0), WorkspaceModel::FocusedRole).toBool(), true);

    model.replace({
        {.name = QStringLiteral("1"), .output = QStringLiteral("HDMI-A-1"), .number = 1, .focused = false, .visible = true},
        {.name = QStringLiteral("2"), .output = QStringLiteral("HDMI-A-1"), .number = 2, .focused = true, .visible = true},
    });

    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(dataSpy.count(), 2);
    QCOMPARE(model.data(model.index(1, 0), WorkspaceModel::FocusedRole).toBool(), true);
}

void WindowManagerTests::windowModelMutatesIncrementally()
{
    WindowModel model;
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy movedSpy(&model, &QAbstractItemModel::rowsMoved);
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

    model.replace({
        {.id = 1, .title = QStringLiteral("Terminal"), .appId = QStringLiteral("foot"), .workspaceName = QStringLiteral("1"), .monitor = QString()},
        {.id = 2, .title = QStringLiteral("Browser"), .appId = QStringLiteral("firefox"), .workspaceName = QStringLiteral("1"), .monitor = QString()},
    });

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 2);
    QCOMPARE(model.rowCount(), 2);

    model.replace({
        {.id = 1, .title = QStringLiteral("Terminal"), .appId = QStringLiteral("foot"), .workspaceName = QStringLiteral("1"), .monitor = QString()},
        {.id = 3, .title = QStringLiteral("Editor"), .appId = QStringLiteral("code"), .workspaceName = QStringLiteral("1"), .monitor = QString()},
        {.id = 2, .title = QStringLiteral("Browser"), .appId = QStringLiteral("firefox"), .workspaceName = QStringLiteral("1"), .monitor = QString()},
    });

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 3);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(1, 0), WindowModel::IdRole).toLongLong(), 3);

    model.replace({
        {.id = 3, .title = QStringLiteral("Editor"), .appId = QStringLiteral("code"), .workspaceName = QStringLiteral("1"), .monitor = QString(), .focused = true},
        {.id = 1, .title = QStringLiteral("Terminal"), .appId = QStringLiteral("foot"), .workspaceName = QStringLiteral("1"), .monitor = QString()},
        {.id = 2, .title = QStringLiteral("Browser"), .appId = QStringLiteral("firefox"), .workspaceName = QStringLiteral("1"), .monitor = QString()},
    });

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(dataSpy.count(), 1);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0, 0), WindowModel::IdRole).toLongLong(), 3);
    QCOMPARE(model.data(model.index(0, 0), WindowModel::FocusedRole).toBool(), true);

    model.replace({
        {.id = 3, .title = QStringLiteral("Editor"), .appId = QStringLiteral("code"), .workspaceName = QStringLiteral("1"), .monitor = QString(), .focused = true},
        {.id = 2, .title = QStringLiteral("Browser"), .appId = QStringLiteral("firefox"), .workspaceName = QStringLiteral("1"), .monitor = QString()},
    });

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), WindowModel::IdRole).toLongLong(), 3);
    QCOMPARE(model.data(model.index(0, 0), WindowModel::FocusedRole).toBool(), true);
}

void WindowManagerTests::i3WorkspaceJsonIsParsed()
{
    const QByteArray json = R"([
        {"name":"1","num":1,"output":"DP-1","focused":false,"urgent":false,"visible":true},
        {"name":"5","num":5,"output":"DP-1","focused":true,"urgent":true,"visible":true}
    ])";

    WorkspaceModel model;
    model.replaceFromI3Json(QJsonDocument::fromJson(json).array());

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(1, 0), WorkspaceModel::NameRole).toString(), QStringLiteral("5"));
    QCOMPARE(model.data(model.index(1, 0), WorkspaceModel::NumberRole).toInt(), 5);
    QCOMPARE(model.data(model.index(1, 0), WorkspaceModel::UrgentRole).toBool(), true);
}

void WindowManagerTests::hyprlandWorkspaceJsonIsParsed()
{
    const QByteArray workspaces = R"([
        {"id":5,"name":"5","monitor":"DP-1","windows":1},
        {"id":1,"name":"1","monitor":"DP-1","windows":2},
        {"id":9,"name":"9","monitor":"HDMI-A-1","windows":0}
    ])";
    const QByteArray active = R"({"id":5,"name":"5"})";
    const QByteArray monitors = R"([
        {"name":"DP-1","activeWorkspace":{"id":5,"name":"5"}},
        {"name":"HDMI-A-1","activeWorkspace":{"id":9,"name":"9"}}
    ])";

    const auto parsed = HyprlandBackend::parseWorkspaces(workspaces, active, monitors);

    QCOMPARE(parsed.size(), 3);
    QCOMPARE(parsed.at(0).name, QStringLiteral("1"));
    QCOMPARE(parsed.at(1).name, QStringLiteral("5"));
    QCOMPARE(parsed.at(1).focused, true);
    QCOMPARE(parsed.at(1).visible, true);
    QCOMPARE(parsed.at(2).visible, true);
}

void WindowManagerTests::hyprlandActiveWindowTitleIsParsed()
{
    const QByteArray activeWindow = R"({"address":"0x123","class":"code","title":"main.cpp - qbar"})";

    QCOMPARE(HyprlandBackend::parseActiveWindowTitle(activeWindow), QStringLiteral("main.cpp - qbar"));
}

void WindowManagerTests::hyprlandKeyboardLayoutIsNormalized()
{
    const QByteArray devices = R"({
        "keyboards": [
            {"name":"keyboard","active_keymap":"Brazilian Portuguese"}
        ]
    })";

    QCOMPARE(HyprlandBackend::parseActiveKeyboardLayout(devices), QStringLiteral("br"));
}

void WindowManagerTests::hyprlandSubmapIsNormalizedAsBindingMode()
{
    QCOMPARE(HyprlandBackend::normalizeSubmapName(QStringLiteral("resize")), QStringLiteral("resize"));
    QCOMPARE(HyprlandBackend::normalizeSubmapName(QStringLiteral(" resize ")), QStringLiteral("resize"));
    QCOMPARE(HyprlandBackend::normalizeSubmapName(QString()), QStringLiteral("default"));
    QCOMPARE(HyprlandBackend::normalizeSubmapName(QStringLiteral("reset")), QStringLiteral("default"));
}

void WindowManagerTests::bspwmDesktopStateIsParsed()
{
    const auto parsed = BspwmBackend::parseDesktopState(
        {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("web")},
        QStringLiteral("2"),
        {QStringLiteral("1"), QStringLiteral("2")},
        {QStringLiteral("web")});

    QCOMPARE(parsed.size(), 3);
    QCOMPARE(parsed.at(0).name, QStringLiteral("1"));
    QCOMPARE(parsed.at(1).focused, true);
    QCOMPARE(parsed.at(1).visible, true);
    QCOMPARE(parsed.at(2).urgent, true);
    QCOMPARE(parsed.at(2).number, 3);
}

void WindowManagerTests::bspwmFocusedNodeTitleIsParsed()
{
    const QByteArray node = R"({
        "id": 123,
        "client": {
            "className": "firefox",
            "name": "QBar issue tracker"
        }
    })";

    QCOMPARE(BspwmBackend::parseFocusedNodeTitle(node), QStringLiteral("QBar issue tracker"));
}

void WindowManagerTests::factoryCreatesNullBackend()
{
    std::unique_ptr<WindowManagerBackend> backend(createWindowManagerBackend(QStringLiteral("none"), nullptr));
    QVERIFY(backend != nullptr);
    QCOMPARE(backend->name(), QStringLiteral("none"));
    QVERIFY(backend->workspaceModel()->isEmpty());
}

QTEST_GUILESS_MAIN(WindowManagerTests)
