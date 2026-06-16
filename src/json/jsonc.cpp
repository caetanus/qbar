#include "jsonc.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace {

class Parser {
public:
    explicit Parser(const QString &text)
        : m_text(text)
    {
    }

    QJsonValue parse(QString *error)
    {
        skipWhitespace();
        const QJsonValue value = parseValue();
        if (m_ok) {
            skipWhitespace();
            if (m_pos != m_text.length()) {
                fail(QStringLiteral("trailing characters"));
            }
        }
        if (!m_ok && error != nullptr) {
            *error = QStringLiteral("JSON parse error at offset %1: %2").arg(m_errorPos).arg(m_error);
        }
        return m_ok ? value : QJsonValue();
    }

private:
    const QString &m_text;
    int m_pos = 0;
    bool m_ok = true;
    QString m_error;
    int m_errorPos = 0;

    void fail(const QString &message)
    {
        if (m_ok) {
            m_ok = false;
            m_error = message;
            m_errorPos = m_pos;
        }
    }

    bool atEnd() const { return m_pos >= m_text.length(); }
    QChar peek() const { return atEnd() ? QChar() : m_text.at(m_pos); }
    QChar peek(int ahead) const
    {
        const int p = m_pos + ahead;
        return p < m_text.length() ? m_text.at(p) : QChar();
    }

    void skipWhitespace()
    {
        while (!atEnd()) {
            const QChar c = m_text.at(m_pos);
            if (c == u' ' || c == u'\t' || c == u'\n' || c == u'\r') {
                ++m_pos;
            } else if (c == u'/' && peek(1) == u'/') {
                m_pos += 2;
                while (!atEnd() && m_text.at(m_pos) != u'\n') {
                    ++m_pos;
                }
            } else if (c == u'/' && peek(1) == u'*') {
                m_pos += 2;
                while (!atEnd() && !(m_text.at(m_pos) == u'*' && peek(1) == u'/')) {
                    ++m_pos;
                }
                if (atEnd()) {
                    fail(QStringLiteral("unterminated block comment"));
                    return;
                }
                m_pos += 2;
            } else {
                break;
            }
        }
    }

    bool consumeLiteral(QLatin1String literal)
    {
        if (m_text.mid(m_pos, literal.size()) == literal) {
            m_pos += literal.size();
            return true;
        }
        return false;
    }

    QJsonValue parseValue()
    {
        skipWhitespace();
        if (!m_ok) {
            return {};
        }
        const QChar c = peek();
        if (c == u'{') {
            return parseObject();
        }
        if (c == u'[') {
            return parseArray();
        }
        if (c == u'"') {
            return QJsonValue(parseString());
        }
        if (c == u'-' || (c >= u'0' && c <= u'9')) {
            return parseNumber();
        }
        if (consumeLiteral(QLatin1String("true"))) {
            return QJsonValue(true);
        }
        if (consumeLiteral(QLatin1String("false"))) {
            return QJsonValue(false);
        }
        if (consumeLiteral(QLatin1String("null"))) {
            return QJsonValue(QJsonValue::Null);
        }
        fail(QStringLiteral("unexpected token"));
        return {};
    }

    QJsonValue parseObject()
    {
        QJsonObject object;
        ++m_pos; // consume '{'
        skipWhitespace();
        if (peek() == u'}') {
            ++m_pos;
            return object;
        }

        while (m_ok) {
            skipWhitespace();
            if (peek() != u'"') {
                fail(QStringLiteral("expected string key"));
                return {};
            }
            const QString key = parseString();
            if (!m_ok) {
                return {};
            }
            skipWhitespace();
            if (peek() != u':') {
                fail(QStringLiteral("expected ':'"));
                return {};
            }
            ++m_pos;
            const QJsonValue value = parseValue();
            if (!m_ok) {
                return {};
            }
            object.insert(key, value);

            skipWhitespace();
            const QChar c = peek();
            if (c == u',') {
                ++m_pos;
                skipWhitespace();
                if (peek() == u'}') { // trailing comma
                    ++m_pos;
                    return object;
                }
                continue;
            }
            if (c == u'}') {
                ++m_pos;
                return object;
            }
            fail(QStringLiteral("expected ',' or '}'"));
            return {};
        }
        return {};
    }

    QJsonValue parseArray()
    {
        QJsonArray array;
        ++m_pos; // consume '['
        skipWhitespace();
        if (peek() == u']') {
            ++m_pos;
            return array;
        }

        while (m_ok) {
            const QJsonValue value = parseValue();
            if (!m_ok) {
                return {};
            }
            array.append(value);

            skipWhitespace();
            const QChar c = peek();
            if (c == u',') {
                ++m_pos;
                skipWhitespace();
                if (peek() == u']') { // trailing comma
                    ++m_pos;
                    return array;
                }
                continue;
            }
            if (c == u']') {
                ++m_pos;
                return array;
            }
            fail(QStringLiteral("expected ',' or ']'"));
            return {};
        }
        return {};
    }

    QString parseString()
    {
        QString out;
        ++m_pos; // consume opening '"'
        while (!atEnd()) {
            const QChar c = m_text.at(m_pos++);
            if (c == u'"') {
                return out;
            }
            if (c == u'\\') {
                if (atEnd()) {
                    break;
                }
                const QChar esc = m_text.at(m_pos++);
                switch (esc.unicode()) {
                case '"': out.append(u'"'); break;
                case '\\': out.append(u'\\'); break;
                case '/': out.append(u'/'); break;
                case 'b': out.append(u'\b'); break;
                case 'f': out.append(u'\f'); break;
                case 'n': out.append(u'\n'); break;
                case 'r': out.append(u'\r'); break;
                case 't': out.append(u'\t'); break;
                case 'u': out.append(parseUnicodeEscape()); break;
                default:
                    fail(QStringLiteral("invalid string escape"));
                    return {};
                }
                if (!m_ok) {
                    return {};
                }
            } else {
                out.append(c);
            }
        }
        fail(QStringLiteral("unterminated string"));
        return {};
    }

    QChar parseUnicodeEscape()
    {
        if (m_pos + 4 > m_text.length()) {
            fail(QStringLiteral("invalid \\u escape"));
            return {};
        }
        bool okHex = false;
        const int code = m_text.mid(m_pos, 4).toInt(&okHex, 16);
        if (!okHex) {
            fail(QStringLiteral("invalid \\u escape"));
            return {};
        }
        m_pos += 4;
        return QChar(static_cast<char16_t>(code));
    }

    QJsonValue parseNumber()
    {
        const int start = m_pos;
        if (peek() == u'-') {
            ++m_pos;
        }
        while (peek().isDigit()) {
            ++m_pos;
        }
        bool isDouble = false;
        if (peek() == u'.') {
            isDouble = true;
            ++m_pos;
            while (peek().isDigit()) {
                ++m_pos;
            }
        }
        if (peek() == u'e' || peek() == u'E') {
            isDouble = true;
            ++m_pos;
            if (peek() == u'+' || peek() == u'-') {
                ++m_pos;
            }
            while (peek().isDigit()) {
                ++m_pos;
            }
        }

        const QString token = m_text.mid(start, m_pos - start);
        bool okNum = false;
        if (!isDouble) {
            const qint64 asInt = token.toLongLong(&okNum);
            if (okNum) {
                return QJsonValue(asInt);
            }
        }
        const double asDouble = token.toDouble(&okNum);
        if (!okNum) {
            fail(QStringLiteral("invalid number"));
            return {};
        }
        return QJsonValue(asDouble);
    }
};

} // namespace

namespace Jsonc {

QJsonDocument parse(const QString &text, QString *error)
{
    Parser parser(text);
    const QJsonValue value = parser.parse(error);
    if (value.isObject()) {
        return QJsonDocument(value.toObject());
    }
    if (value.isArray()) {
        return QJsonDocument(value.toArray());
    }
    return {};
}

} // namespace Jsonc
