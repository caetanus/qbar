#pragma once

#include <QObject>

class JsoncTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesPlainJson();
    void parsesLineComments();
    void parsesBlockComments();
    void allowsTrailingCommas();
    void keepsCommentMarkersInsideStrings();
    void reportsErrors();
};
