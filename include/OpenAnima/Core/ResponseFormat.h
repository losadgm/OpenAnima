/// @file
/// @brief The shape a caller asks the answer to arrive in.

#pragma once

#include <cassert>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <utility>

namespace OpenAnima::Core {

/// What a caller expects the model to answer with.
///
/// The chat-completions contract spells this as `response_format`, and a server that does
/// not know the field ignores it. What arrives is then checked here instead: as soon as a
/// shape other than text is asked for, the answer is read as JSON, and an answer that is
/// not JSON is a ParseError rather than something the caller has to discover.
struct ResponseFormat {
    /// The shapes an answer can be asked for in.
    enum class Kind : std::uint8_t {
        Text,        ///< Free text, which is what a caller that asks for nothing gets.
        JsonObject,  ///< A JSON object, with no contract on what it holds.
        JsonSchema   ///< A JSON object that follows a schema, which is sent with the request.
    };

    Kind kind{Kind::Text};  ///< Which of the three is being asked for.
    nlohmann::json schema;  ///< The schema, used when the kind is JsonSchema.

    /// @return Free text, which is what a caller that asks for nothing gets.
    [[nodiscard]] static ResponseFormat text() {
        return ResponseFormat{};
    }

    /// @return A JSON object, with no contract on what it holds.
    [[nodiscard]] static ResponseFormat jsonObject() {
        return ResponseFormat{Kind::JsonObject, {}};
    }

    /// @param schema JSON Schema the answer has to follow, sent as it is given.
    /// @return An answer shaped by that schema.
    [[nodiscard]] static ResponseFormat jsonSchema(nlohmann::json schema) {
        assert(!schema.is_null() && "a schema is required");
        return ResponseFormat{Kind::JsonSchema, std::move(schema)};
    }
};

}  // namespace OpenAnima::Core
