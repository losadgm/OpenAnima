/// @file
/// @brief Splits a `text/event-stream` body into the payloads of its events.
///
/// An implementation detail of the provider: it lives next to its implementation
/// and is not installed, because what a caller cares about is the answer, not the
/// framing the answer arrived in.

#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace OpenAnima::Provider {

/// Reassembles a `text/event-stream` body into the payload of each of its events.
///
/// Bytes arrive in whatever pieces the network hands over: one can carry half a
/// line, several lines, or stop in the middle of a UTF-8 sequence. The reader keeps
/// the unfinished tail and hands out one payload per event, which for a
/// chat-completions stream is the JSON of one chunk.
///
/// Only the `data` field is kept. Comments (the keep-alives a server sends to hold a
/// connection open), `event`, `id` and `retry` are read and dropped, as the format
/// says, and several `data` lines in one event are joined with a newline.
class SseReader {
public:
    /// Called with the payload of one complete event.
    /// @return false to stop reading.
    using PayloadSink = std::function<bool(std::string_view payload)>;

    /// Feeds the next piece of the body.
    /// @param piece Bytes just received.
    /// @param onPayload Called once per complete event, in the order they arrived.
    /// @return false when the sink asked to stop, which stops the whole feed.
    bool feed(std::string_view piece, const PayloadSink& onPayload);

private:
    /// Handles a line that has just ended, and dispatches the event when it is blank.
    bool endLine(const PayloadSink& onPayload);

    /// Hands the event collected so far to the sink.
    bool dispatch(const PayloadSink& onPayload);

    std::string m_line;             ///< Bytes of the line being assembled.
    std::string m_payload;          ///< `data` values collected for the current event.
    bool m_sawData{false};          ///< Whether the current event carried a `data` field.
    bool m_swallowLineFeed{false};  ///< The previous byte was a carriage return.
};

}  // namespace OpenAnima::Provider
