#pragma once

#include <QObject>

class CustomToolModelTests final : public QObject {
    Q_OBJECT

private slots:
    void appliesFormatPlaceholders();
    void resolvesFormatIconFromAlt();
    void convertsPangoMarkupToRichText();
    void leavesEscapedAndInvalidPlaceholdersUntouched();
    void fallsBackToPlainTextWhenOutputIsNotJson();
};
