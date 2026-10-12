#include "OpenAnima/Provider/SseReader.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace OpenAnima::Provider {

namespace {

/// @param line A complete line of the stream.
/// @return The value of the field, with one leading space removed, or nothing when
/// the line carries no colon.
std::string_view fieldValue(const std::string& line) {
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return {};
    }
    std::string_view value(line);
    value.remove_prefix(colon + 1);
    if (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
    }
    return value;
}

/// @param line A complete line of the stream.
/// @return The name of the field, empty for a blank line.
std::string_view fieldName(const std::string& line) {
    const std::size_t colon = line.find(':');
    return colon == std::string::npos ? std::string_view(line)
                                      : std::string_view(line).substr(0, colon);
}

}  // namespace

bool SseReader::feed(std::string_view piece, const PayloadSink& onPayload) {
    // Reading a body is walking its bytes in order and stopping at the first one the
    // sink rejects, which is exactly what all_of does with a predicate that keeps
    // state: the name is the only odd thing about using it here.
    return std::all_of(piece.begin(), piece.end(), [this, &onPayload](char character) {
        return consume(character, onPayload);
    });
}

bool SseReader::consume(char character, const PayloadSink& onPayload) {
    if (m_swallowLineFeed) {
        m_swallowLineFeed = false;
        if (character == '\n') {
            // The second half of a CRLF, not a line end of its own.
            return true;
        }
    }

    if (character == '\r' || character == '\n') {
        m_swallowLineFeed = character == '\r';
        return endLine(onPayload);
    }

    m_line += character;
    return true;
}

bool SseReader::endLine(const PayloadSink& onPayload) {
    const bool blank = m_line.empty();
    if (!blank && m_line.front() != ':') {
        // A line starting with a colon is a comment, which is what a keep-alive is.
        if (fieldName(m_line) == "data") {
            if (m_sawData) {
                m_payload += '\n';
            }
            m_payload += fieldValue(m_line);
            m_sawData = true;
        }
        // Every other field is read and dropped.
    }
    m_line.clear();

    if (!blank) {
        return true;
    }
    return dispatch(onPayload);
}

bool SseReader::dispatch(const PayloadSink& onPayload) {
    if (!m_sawData) {
        // A comment on its own, or the blank line that follows an event.
        return true;
    }

    // An empty data buffer is not an event: `data:` with nothing after it is one of the
    // heartbeats a server sends to hold the connection open, not the payload of a chunk.
    const bool carriesPayload = !m_payload.empty();
    m_sawData = false;
    if (!carriesPayload) {
        return true;
    }

    const bool keepGoing = onPayload(m_payload);
    m_payload.clear();
    return keepGoing;
}

}  // namespace OpenAnima::Provider
