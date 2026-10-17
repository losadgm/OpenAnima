#include "OpenAnima/Core/ReplyStream.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace OpenAnima::Core {
namespace {

/// @param character Character to test.
/// @return Whether it is a hexadecimal digit.
bool isHexDigit(char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

/// @param character Hexadecimal digit.
/// @return Its value.
std::uint32_t hexValue(char character) {
    if (character >= '0' && character <= '9') {
        return static_cast<std::uint32_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        return static_cast<std::uint32_t>(character - 'a') + 10U;
    }
    return static_cast<std::uint32_t>(character - 'A') + 10U;
}

}  // namespace

ReplyStream::ReplyStream(std::string field) : m_field(std::move(field)) {}

std::string ReplyStream::feed(std::string_view piece) {
    std::string emitted;
    emitted.reserve(piece.size());
    for (const char character : piece) {
        consume(character, emitted);
    }
    return emitted;
}

void ReplyStream::consume(char character, std::string& emitted) {
    if (m_mode == Mode::Outside) {
        consumeOutside(character);
        return;
    }
    consumeInString(character, emitted);
}

void ReplyStream::consumeOutside(char character) {
    switch (character) {
    case '"':
        openString();
        break;
    case '{':
        ++m_depth;
        if (m_depth == 1) {
            m_expectingKey = true;
        }
        break;
    case '[':
        ++m_depth;
        break;
    case '}':
    case ']':
        if (m_depth > 0) {
            --m_depth;
        }
        break;
    case ',':
    case ':':
        if (m_depth == 1) {
            m_expectingKey = character == ',';
        }
        break;
    default:
        break;
    }
}

void ReplyStream::openString() {
    if (m_depth == 1 && m_expectingKey) {
        m_mode = Mode::InKey;
        m_key.clear();
    } else if (m_depth == 1 && m_key == m_field) {
        m_mode = Mode::InValue;
    } else {
        m_mode = Mode::Skipping;
    }
}

void ReplyStream::closeString() {
    const bool wasKey = m_mode == Mode::InKey;
    m_mode = Mode::Outside;
    if (wasKey) {
        m_expectingKey = false;  // the colon that follows is not a key.
    }
}

void ReplyStream::consumeInString(char character, std::string& emitted) {
    if (m_unicode) {
        if (isHexDigit(character)) {
            m_hex.push_back(character);
            if (m_hex.size() == 4) {
                m_unicode = false;
                finishUnicode(emitted);
            }
            return;
        }
        // Not a digit: the escape was cut short, so it is dropped and this character is
        // read as what it is.
        m_unicode = false;
        m_hex.clear();
    }

    if (m_escape) {
        m_escape = false;
        consumeEscape(character, emitted);
        return;
    }

    if (character == '\\') {
        m_escape = true;
        return;
    }

    if (character == '"') {
        // The pair is flushed before the string closes, while the text still belongs to
        // the field.
        flushSurrogate(emitted);
        closeString();
        return;
    }

    flushSurrogate(emitted);
    appendByte(character, emitted);
}

void ReplyStream::consumeEscape(char character, std::string& emitted) {
    switch (character) {
    case 'u':
        m_unicode = true;
        m_hex.clear();
        return;
    case 'n':
        flushSurrogate(emitted);
        appendByte('\n', emitted);
        return;
    case 't':
        flushSurrogate(emitted);
        appendByte('\t', emitted);
        return;
    case 'r':
        flushSurrogate(emitted);
        appendByte('\r', emitted);
        return;
    case 'b':
        flushSurrogate(emitted);
        appendByte('\b', emitted);
        return;
    case 'f':
        flushSurrogate(emitted);
        appendByte('\f', emitted);
        return;
    default:
        // "\"", "\\" and "\/", and anything JSON does not define, which is passed on.
        flushSurrogate(emitted);
        appendByte(character, emitted);
        return;
    }
}

void ReplyStream::finishUnicode(std::string& emitted) {
    std::uint32_t value = 0;
    for (const char digit : m_hex) {
        value = (value << 4U) | hexValue(digit);
    }
    m_hex.clear();

    if (value >= 0xD800U && value <= 0xDBFFU) {
        flushSurrogate(emitted);
        m_highSurrogate = value;
        return;
    }
    if (value >= 0xDC00U && value <= 0xDFFFU) {
        if (m_highSurrogate != 0) {
            const std::uint32_t code =
                0x10000U + ((m_highSurrogate - 0xD800U) << 10U) + (value - 0xDC00U);
            m_highSurrogate = 0;
            appendCodePoint(code, emitted);
            return;
        }
        appendCodePoint(0xFFFDU, emitted);  // a low half on its own.
        return;
    }

    flushSurrogate(emitted);
    appendCodePoint(value, emitted);
}

void ReplyStream::flushSurrogate(std::string& emitted) {
    if (m_highSurrogate == 0) {
        return;
    }
    m_highSurrogate = 0;
    appendCodePoint(0xFFFDU, emitted);
}

void ReplyStream::appendByte(char character, std::string& emitted) {
    if (m_mode == Mode::InKey) {
        m_key.push_back(character);
    } else if (m_mode == Mode::InValue) {
        m_text.push_back(character);
        emitted.push_back(character);
    }
}

void ReplyStream::appendCodePoint(std::uint32_t codePoint, std::string& emitted) {
    if (codePoint <= 0x7FU) {
        appendByte(static_cast<char>(codePoint), emitted);
    } else if (codePoint <= 0x7FFU) {
        appendByte(static_cast<char>(0xC0U | (codePoint >> 6U)), emitted);
        appendByte(static_cast<char>(0x80U | (codePoint & 0x3FU)), emitted);
    } else if (codePoint <= 0xFFFFU) {
        appendByte(static_cast<char>(0xE0U | (codePoint >> 12U)), emitted);
        appendByte(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)), emitted);
        appendByte(static_cast<char>(0x80U | (codePoint & 0x3FU)), emitted);
    } else {
        appendByte(static_cast<char>(0xF0U | (codePoint >> 18U)), emitted);
        appendByte(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)), emitted);
        appendByte(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)), emitted);
        appendByte(static_cast<char>(0x80U | (codePoint & 0x3FU)), emitted);
    }
}

}  // namespace OpenAnima::Core
