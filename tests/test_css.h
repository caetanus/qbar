#pragma once

#include <QObject>

class CssThemeTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesIdClassAndSourceOrder();
    void resolvesDescendantSelectorsWithContext();
    void exactResolveIgnoresUniversalRules();
    void stripsCommentsAndAtRules();
    void parsesColors();
};
