/// @file
/// @brief Reads one string out of a JSON document while it is still arriving.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace OpenAnima::Core {

/// Follows a JSON document as it arrives and collects the value of one top-level string.
///
/// The answer is asked for as JSON, so a sink that painted it would paint braces and
/// keys. This reads the same bytes and hands back the characters of one field, decoded,
/// as soon as they are known, including an escape sequence split across two pieces.
///
/// The field is looked for at the top level only, and the keys may come in any order: a
/// document that starts with the dialogue still yields it. A repeated key is not valid
/// JSON and its text is appended to what the first one produced.
class ReplyStream {
public:
    /// @param field Top-level key whose string value is collected.
    explicit ReplyStream(std::string field);

    /// @param piece Next piece of the document, as the provider sent it.
    /// @return The characters of the field this piece completed, already decoded.
    std::string feed(std::string_view piece);

    /// @return Everything the field has produced so far.
    [[nodiscard]] const std::string& text() const noexcept {
        return m_text;
    }

private:
    /// What the reader is doing inside the document.
    enum class Mode : std::uint8_t {
        Outside,  ///< Between tokens.
        InKey,    ///< Inside a top-level key.
        InValue,  ///< Inside the value of the collected field.
        Skipping  ///< Inside a string that is neither.
    };

    /// Consumes one character of the document.
    /// @param character Next character.
    /// @param emitted Characters the field gained.
    void consume(char character, std::string& emitted);

    /// Consumes one character that is not inside a string.
    /// @param character Next character.
    void consumeOutside(char character);

    /// Opens the string that starts with the quote just read.
    void openString();

    /// Closes the string that just ended.
    void closeString();

    /// Consumes one character inside a string.
    /// @param character Next character.
    /// @param emitted Characters the field gained.
    void consumeInString(char character, std::string& emitted);

    /// Consumes the character that follows a backslash.
    /// @param character Next character.
    /// @param emitted Characters the field gained.
    void consumeEscape(char character, std::string& emitted);

    /// Reads the digits of a \u escape that just completed.
    /// @param emitted Characters the field gained.
    void finishUnicode(std::string& emitted);

    /// Drops a high surrogate that never got its pair.
    /// @param emitted Characters the field gained.
    void flushSurrogate(std::string& emitted);

    /// Appends one byte of the document, if it belongs to the field.
    /// @param character Byte to append.
    /// @param emitted Characters the field gained.
    void appendByte(char character, std::string& emitted);

    /// Appends one code point as UTF-8, if it belongs to the field.
    /// @param codePoint Code point to append.
    /// @param emitted Characters the field gained.
    void appendCodePoint(std::uint32_t codePoint, std::string& emitted);

    std::string m_field;               ///< Key being collected.
    std::string m_text;                ///< Text collected so far.
    std::string m_key;                 ///< Key being read, or the last one read.
    std::string m_hex;                 ///< Hex digits of a \u escape read so far.
    std::uint32_t m_highSurrogate{0};  ///< Pending high surrogate, zero when there is none.
    int m_depth{0};                    ///< Objects and arrays opened and not closed.
    bool m_escape{false};              ///< Whether the last character was a backslash.
    bool m_unicode{false};             ///< Whether the digits of a \u escape are being read.
    bool m_expectingKey{false};        ///< Whether the next string is a key of the top object.
    Mode m_mode{Mode::Outside};        ///< What the reader is doing.
};

}  // namespace OpenAnima::Core
