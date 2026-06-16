#include "test_css.h"

#include "src/css/csstheme.h"

#include <QSignalSpy>
#include <QTest>

void CssThemeTests::parsesIdClassAndSourceOrder()
{
    CssTheme theme;
    QSignalSpy loadedSpy(&theme, &CssTheme::loadedChanged);

    theme.loadFromString(QStringLiteral(R"(
        * { color: #eeeeee; }
        #battery { background-color: #222222; color: #ffffff; }
        #battery.charging { background-color: #218f4f; }
        #battery { color: #101010; }
    )"));

    QVERIFY(theme.isLoaded());
    QCOMPARE(loadedSpy.count(), 1);

    const QVariantMap base = theme.resolve(QStringLiteral("battery"));
    QCOMPARE(base.value(QStringLiteral("background-color")).toString(), QStringLiteral("#222222"));
    QCOMPARE(base.value(QStringLiteral("color")).toString(), QStringLiteral("#101010"));

    const QVariantMap charging = theme.resolve(QStringLiteral("battery"), {QStringLiteral("charging")});
    QCOMPARE(charging.value(QStringLiteral("background-color")).toString(), QStringLiteral("#218f4f"));
    QCOMPARE(charging.value(QStringLiteral("color")).toString(), QStringLiteral("#101010"));
}

void CssThemeTests::resolvesDescendantSelectorsWithContext()
{
    CssTheme theme;
    theme.loadFromString(QStringLiteral(R"(
        button { color: #111111; }
        #workspaces button { color: #eeeeee; padding-left: 2px; }
        #workspaces button.focused { color: #ffffff; background-color: #2f97d1; }
        #tray button.focused { color: #ff00ff; }
    )"));

    const QVariantMap topLevel = theme.resolve(QStringLiteral("button"));
    QVERIFY(!topLevel.contains(QStringLiteral("color")));

    const QVariantMap workspaceButton = theme.resolveWith(QStringLiteral("workspaces"), QStringLiteral("button"));
    QCOMPARE(workspaceButton.value(QStringLiteral("color")).toString(), QStringLiteral("#eeeeee"));
    QCOMPARE(workspaceButton.value(QStringLiteral("padding-left")).toString(), QStringLiteral("2px"));

    const QVariantMap focusedWorkspaceButton = theme.resolveWith(QStringLiteral("workspaces"),
                                                                 QStringLiteral("button"),
                                                                 {QStringLiteral("focused")});
    QCOMPARE(focusedWorkspaceButton.value(QStringLiteral("color")).toString(), QStringLiteral("#ffffff"));
    QCOMPARE(focusedWorkspaceButton.value(QStringLiteral("background-color")).toString(), QStringLiteral("#2f97d1"));

    const QVariantMap trayButton = theme.resolveWith(QStringLiteral("tray"), QStringLiteral("button"), {QStringLiteral("focused")});
    QCOMPARE(trayButton.value(QStringLiteral("color")).toString(), QStringLiteral("#ff00ff"));
}

void CssThemeTests::exactResolveIgnoresUniversalRules()
{
    CssTheme theme;
    theme.loadFromString(QStringLiteral(R"(
        * { color: #eeeeee; }
        #clock { color: #ffffff; }
        #clock.warning { background-color: #ff0000; }
        #workspaces #clock { color: #00ff00; }
    )"));

    const QVariantMap exact = theme.resolveExact(QStringLiteral("clock"));
    QCOMPARE(exact.size(), 1);
    QCOMPARE(exact.value(QStringLiteral("color")).toString(), QStringLiteral("#ffffff"));

    const QVariantMap warning = theme.resolveExact(QStringLiteral("clock"), {QStringLiteral("warning")});
    QCOMPARE(warning.value(QStringLiteral("color")).toString(), QStringLiteral("#ffffff"));
    QCOMPARE(warning.value(QStringLiteral("background-color")).toString(), QStringLiteral("#ff0000"));
}

void CssThemeTests::stripsCommentsAndAtRules()
{
    CssTheme theme;
    theme.loadFromString(QStringLiteral(R"(
        /* This should not create a selector. */
        @define-color accent #63b3ed;
        #cpu {
            color: @accent;
            graph-width: 22px;
        }
    )"));

    const QVariantMap cpu = theme.resolve(QStringLiteral("cpu"));
    // @define-color is expanded: the @accent reference resolves to its value
    // and the @define-color declaration itself is stripped.
    QCOMPARE(cpu.value(QStringLiteral("color")).toString(), QStringLiteral("#63b3ed"));
    QCOMPARE(cpu.value(QStringLiteral("graph-width")).toString(), QStringLiteral("22px"));
}

void CssThemeTests::parsesColors()
{
    CssTheme theme;

    QCOMPARE(theme.parseColor(QStringLiteral("transparent")), QColor(0, 0, 0, 0));
    QCOMPARE(theme.parseColor(QStringLiteral("#112233")), QColor(QStringLiteral("#112233")));
    QCOMPARE(theme.parseColor(QStringLiteral("rgb(1, 2, 3)")), QColor(1, 2, 3));

    const QColor rgba = theme.parseColor(QStringLiteral("rgba(10, 20, 30, 0.5)"));
    QCOMPARE(rgba.red(), 10);
    QCOMPARE(rgba.green(), 20);
    QCOMPARE(rgba.blue(), 30);
    QVERIFY(rgba.alpha() >= 127 && rgba.alpha() <= 128);
}

QTEST_GUILESS_MAIN(CssThemeTests)
