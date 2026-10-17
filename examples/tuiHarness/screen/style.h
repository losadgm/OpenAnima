/// @file
/// @brief Colour and style as values, so what the terminal has been told can be compared.

#pragma once

#include <array>
#include <cstdint>

namespace OpenAnimaTui::Screen {

/// A colour the terminal can paint: its own default, a palette index, or 24-bit RGB.
struct Color {
    /// Which of the three ways the colour is given.
    enum class Kind : std::uint8_t { Default, Indexed, Rgb };

    Kind kind{Kind::Default};  ///< How to read the fields below.
    std::uint8_t index{0};     ///< Palette entry, for Kind::Indexed.
    std::uint8_t red{0};       ///< Red channel, for Kind::Rgb.
    std::uint8_t green{0};     ///< Green channel, for Kind::Rgb.
    std::uint8_t blue{0};      ///< Blue channel, for Kind::Rgb.

    /// @return The colour the terminal uses when nothing is asked for.
    [[nodiscard]] static Color defaultValue();

    /// @param index Palette entry, 0 to 255.
    /// @return That palette entry.
    [[nodiscard]] static Color indexed(std::uint8_t index);

    /// @param red Red channel.
    /// @param green Green channel.
    /// @param blue Blue channel.
    /// @return That 24-bit colour.
    // A colour is three channels in the order everything in the world spells them.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] static Color rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue);

    /// @param other Colour to compare with.
    /// @return Whether both name the same colour.
    [[nodiscard]] bool equals(const Color& other) const;
};

/// How a cell is painted: a foreground, a background and the display attributes.
///
/// It is a value on purpose. The frame writer keeps the style the terminal is in, compares it
/// with the one a cell asks for, and only speaks when they differ, which is where most of the
/// bytes and most of the flicker go away.
struct Style {
    Color foreground{};     ///< Colour of the glyph.
    Color background{};     ///< Colour behind the glyph.
    bool bold{false};       ///< Heavier, and some terminals use the bright palette entry.
    bool dim{false};        ///< Fainter.
    bool italic{false};     ///< Slanted.
    bool underline{false};  ///< Underlined.
    bool reverse{false};    ///< Foreground and background swapped.

    /// @param colour Colour of the glyph.
    /// @return A copy painted with that foreground.
    [[nodiscard]] Style withForeground(const Color& colour) const;

    /// @param colour Colour behind the glyph.
    /// @return A copy painted on that background.
    [[nodiscard]] Style withBackground(const Color& colour) const;

    /// @param value Whether the style is bold.
    /// @return A copy with that attribute set.
    [[nodiscard]] Style withBold(bool value = true) const;

    /// @param value Whether the style is dim.
    /// @return A copy with that attribute set.
    [[nodiscard]] Style withDim(bool value = true) const;

    /// @param value Whether the style is reversed.
    /// @return A copy with that attribute set.
    [[nodiscard]] Style withReverse(bool value = true) const;

    /// @return Whether the style asks for nothing, which is what a reset leaves behind.
    [[nodiscard]] bool isDefault() const;

    /// @param other Style to compare with.
    /// @return Whether both paint a cell the same way.
    [[nodiscard]] bool equals(const Style& other) const;
};

/// The characters the client draws, which is the one place a glyph is spelled out.
namespace Glyphs {

/// The chevron that marks the line being typed in.
inline constexpr const char* PromptMarker = "\u2771";

/// The braille frames the spinner goes through while a turn is in flight.
inline constexpr std::array<const char*, 10> Spinner = {"\u280B", "\u2819", "\u2839", "\u2838",
                                                        "\u283C", "\u2834", "\u2826", "\u2827",
                                                        "\u2807", "\u280F"};

/// The stroke the scrollbar on the right is drawn with.
inline constexpr const char* ScrollBar = "\u2502";

/// The strokes of the frame around the prompt.
///
/// They are block elements, which paint the part of the cell that hugs one edge, so the frame
/// runs along the inside edge of the panel and nothing of the colour shows outside it. The sides
/// take a quarter of the cell width and the top and the bottom an eighth of its height, which on
/// a cell twice as tall as it is wide is the same two pixels: one weight on all four sides. The
/// corners come from the symbols for legacy computing, where the strips of two edges are joined
/// in one glyph, a eighth each because there is no thicker corner.
inline constexpr const char* BoxTopLeft = "\U0001FB7D";      ///< Left and upper eighth.
inline constexpr const char* BoxTop = "\u2594";              ///< Upper one eighth block.
inline constexpr const char* BoxTopRight = "\U0001FB7E";     ///< Right and upper eighth.
inline constexpr const char* BoxLeft = "\u258E";             ///< Left one quarter block.
inline constexpr const char* BoxRight = "\U0001FB87";        ///< Right one quarter block.
inline constexpr const char* BoxBottomLeft = "\U0001FB7C";   ///< Left and lower eighth.
inline constexpr const char* BoxBottom = "\u2581";           ///< Lower one eighth block.
inline constexpr const char* BoxBottomRight = "\U0001FB7F";  ///< Right and lower eighth.

}  // namespace Glyphs

/// The palette of the client, taken from the OpenCode theme.
namespace Palette {

/// @return The colour of ordinary text.
[[nodiscard]] Style text();

/// @return The colour of the quiet text: notes, hints, the status line.
[[nodiscard]] Style muted();

/// @return The colour of the prompt marker, the spinner and the selected hint.
[[nodiscard]] Style accent();

/// @return The colour of whoever is answering.
[[nodiscard]] Style primary();

/// @return The panel the prompt and the status line sit on.
[[nodiscard]] Style panel();

/// @return The panel the command hints float on.
[[nodiscard]] Style raised();

/// @return The background colour of the panel, to lay under a glyph of another style.
[[nodiscard]] Color panelColor();

/// @return The background colour of the floating hints.
[[nodiscard]] Color raisedColor();

}  // namespace Palette

}  // namespace OpenAnimaTui::Screen
