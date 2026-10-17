#include "style.h"

namespace OpenAnimaTui::Presentation {

namespace {

/// The colours of the theme, as the numbers a terminal is given.
constexpr std::uint8_t Muted = 0x80;
constexpr std::uint8_t AccentRed = 0x9D;
constexpr std::uint8_t AccentGreen = 0x7C;
constexpr std::uint8_t AccentBlue = 0xD8;
constexpr std::uint8_t PrimaryRed = 0xFA;
constexpr std::uint8_t PrimaryGreen = 0xB2;
constexpr std::uint8_t PrimaryBlue = 0x83;
constexpr std::uint8_t TextRed = 0xEE;
constexpr std::uint8_t TextGreen = 0xEE;
constexpr std::uint8_t TextBlue = 0xEE;
constexpr std::uint8_t PanelRed = 0x14;
constexpr std::uint8_t PanelGreen = 0x14;
constexpr std::uint8_t PanelBlue = 0x14;
constexpr std::uint8_t RaisedRed = 0x1E;
constexpr std::uint8_t RaisedGreen = 0x1E;
constexpr std::uint8_t RaisedBlue = 0x1E;

}  // namespace

Color Color::defaultValue() {
    return Color{};
}

Color Color::indexed(std::uint8_t index) {
    Color color;
    color.kind = Kind::Indexed;
    color.index = index;
    return color;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Color Color::rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    Color color;
    color.kind = Kind::Rgb;
    color.red = red;
    color.green = green;
    color.blue = blue;
    return color;
}

bool Color::equals(const Color& other) const {
    return kind == other.kind && index == other.index && red == other.red && green == other.green &&
           blue == other.blue;
}

Style Style::withForeground(const Color& colour) const {
    Style copy = *this;
    copy.foreground = colour;
    return copy;
}

Style Style::withBackground(const Color& colour) const {
    Style copy = *this;
    copy.background = colour;
    return copy;
}

Style Style::withBold(bool value) const {
    Style copy = *this;
    copy.bold = value;
    return copy;
}

Style Style::withDim(bool value) const {
    Style copy = *this;
    copy.dim = value;
    return copy;
}

Style Style::withReverse(bool value) const {
    Style copy = *this;
    copy.reverse = value;
    return copy;
}

Style Style::withUnderline(bool value) const {
    Style copy = *this;
    copy.underline = value;
    return copy;
}

bool Style::isDefault() const {
    return foreground.equals(Color{}) && background.equals(Color{}) && !bold && !dim && !italic &&
           !underline && !reverse;
}

bool Style::equals(const Style& other) const {
    return foreground.equals(other.foreground) && background.equals(other.background) &&
           bold == other.bold && dim == other.dim && italic == other.italic &&
           underline == other.underline && reverse == other.reverse;
}

namespace Palette {

Style text() {
    return Style{}.withForeground(Color::rgb(TextRed, TextGreen, TextBlue));
}

Style muted() {
    return Style{}.withForeground(Color::rgb(Muted, Muted, Muted));
}

Style accent() {
    return Style{}.withForeground(Color::rgb(AccentRed, AccentGreen, AccentBlue));
}

Style primary() {
    return Style{}.withForeground(Color::rgb(PrimaryRed, PrimaryGreen, PrimaryBlue));
}

Style panel() {
    return Style{}.withBackground(panelColor());
}

Style raised() {
    return Style{}.withBackground(raisedColor());
}

Color panelColor() {
    return Color::rgb(PanelRed, PanelGreen, PanelBlue);
}

Color raisedColor() {
    return Color::rgb(RaisedRed, RaisedGreen, RaisedBlue);
}

}  // namespace Palette

}  // namespace OpenAnimaTui::Presentation
