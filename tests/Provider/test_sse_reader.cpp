#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "OpenAnima/Provider/SseReader.h"

using OpenAnima::Provider::SseReader;

namespace {

/// Reads a body delivered in the given pieces.
/// @param pieces Pieces of the body, in the order a socket would deliver them.
/// @return The payload of every event, in order.
std::vector<std::string> read(const std::vector<std::string>& pieces) {
    SseReader reader;
    std::vector<std::string> payloads;
    for (const std::string& piece : pieces) {
        reader.feed(piece, [&payloads](std::string_view payload) {
            payloads.emplace_back(payload);
            return true;
        });
    }
    return payloads;
}

}  // namespace

// Catch2 assertions expand into do-while blocks and test cases are long by design, so
// the rules written for production code do not fit here.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("SseReader hands out one payload per event", "[provider][sse]") {
    const std::vector<std::string> payloads = read({"data: one\n\ndata: two\n\n"});

    REQUIRE(payloads.size() == 2);
    CHECK(payloads[0] == "one");
    CHECK(payloads[1] == "two");
}

TEST_CASE("SseReader reassembles an event split across two pieces", "[provider][sse]") {
    // What a socket produces, and what a reader written against whole lines gets
    // wrong: the piece ends in the middle of the event.
    const std::vector<std::string> payloads = read({"data: {\"a\":", "1}\n\n"});

    REQUIRE(payloads.size() == 1);
    CHECK(payloads[0] == R"({"a":1})");
}

TEST_CASE("SseReader accepts the three line endings", "[provider][sse]") {
    CHECK(read({"data: lf\n\n"}).size() == 1);
    CHECK(read({"data: crlf\r\n\r\n"}).size() == 1);
    CHECK(read({"data: cr\r\r"}).size() == 1);
}

TEST_CASE("SseReader joins the data lines of one event", "[provider][sse]") {
    const std::vector<std::string> payloads = read({"data: first\ndata: second\n\n"});

    REQUIRE(payloads.size() == 1);
    CHECK(payloads[0] == "first\nsecond");
}

TEST_CASE("SseReader drops the comments a server uses to stay connected", "[provider][sse]") {
    const std::vector<std::string> payloads = read({": keep-alive\n\ndata: one\n\n"});

    REQUIRE(payloads.size() == 1);
    CHECK(payloads[0] == "one");
}

TEST_CASE("SseReader drops a data field with nothing in it", "[provider][sse]") {
    // A heartbeat is a `data` field carrying nothing, with or without the space the
    // format strips: it is not an event, so it is not handed over as one.
    CHECK(read({"data:\n\n"}).empty());
    CHECK(read({"data: \n\n"}).empty());

    // And it does not leak into the event that follows it.
    const std::vector<std::string> payloads = read({"data:\n\ndata: one\n\n"});
    REQUIRE(payloads.size() == 1);
    CHECK(payloads[0] == "one");
}

TEST_CASE("SseReader ignores the fields it does not know", "[provider][sse]") {
    const std::vector<std::string> payloads =
        read({"retry: 500\nevent: chunk\nid: 7\ndata: one\n\n"});

    REQUIRE(payloads.size() == 1);
    CHECK(payloads[0] == "one");
}

TEST_CASE("SseReader keeps the sentinel as it is", "[provider][sse]") {
    // The reader knows nothing about the sentinel: telling it apart from a chunk is
    // the provider's business.
    const std::vector<std::string> payloads = read({"data: one\n\ndata: [DONE]\n\n"});

    REQUIRE(payloads.size() == 2);
    CHECK(payloads[1] == "[DONE]");
}

TEST_CASE("SseReader keeps a character split in the middle of its bytes", "[provider][sse]") {
    // The letter n with a tilde is the two bytes C3 B1, and they land in separate
    // pieces. Nothing is decoded piece by piece, so half a character is no problem.
    const std::vector<std::string> payloads = read({"data: \xC3", "\xB1\n\n"});

    REQUIRE(payloads.size() == 1);
    CHECK(payloads[0] == "\xC3\xB1");
}

TEST_CASE("SseReader stops when the sink says so", "[provider][sse]") {
    SseReader reader;
    int seen = 0;
    const bool keepGoing = reader.feed("data: one\n\ndata: two\n\n", [&seen](std::string_view) {
        ++seen;
        return false;
    });

    CHECK_FALSE(keepGoing);
    CHECK(seen == 1);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
