#pragma once

#include <QJsonDocument>
#include <QString>

// Hand-written recursive-descent parser for JSON with comments (JSONC), so the
// config can carry `//` line and `/* */` block comments. Grammar:
//
//   document      = ws value ws
//   value         = object | array | string | number
//                 | "true" | "false" | "null"
//   object        = "{" ws [ member { ws "," ws member } [ ws "," ] ] ws "}"
//   member        = string ws ":" ws value
//   array         = "[" ws [ value { ws "," ws value } [ ws "," ] ] ws "]"
//   (a single trailing comma before the closing bracket is allowed)
//   string        = '"' { char | escape } '"'
//   escape        = "\" ( '"' | "\" | "/" | "b" | "f" | "n" | "r" | "t"
//                       | "u" hex hex hex hex )
//   number        = [ "-" ] int [ "." digit { digit } ] [ ("e"|"E") [ "+"|"-" ] digit { digit } ]
//   ws            = { " " | "\t" | "\n" | "\r" | line_comment | block_comment }
//   line_comment  = "//" { any-but-newline } ( newline | EOF )
//   block_comment = "/*" { any } "*/"
namespace Jsonc {

// Parses JSONC. On failure returns an invalid (Null) document; if `error` is
// non-null it receives a human-readable message with the offset.
QJsonDocument parse(const QString &text, QString *error = nullptr);

} // namespace Jsonc
