#include "cell.h"

#include <algorithm>
#include <string_view>

namespace OpenAnimaTui::Screen {

bool Cell::isBlank() const {
    return kind == Kind::Blank;
}

bool Cell::appendCombining(std::string_view bytes) {
    if (kind != Kind::Glyph || length + bytes.size() > MaxGlyphBytes) {
        return false;
    }
    std::copy(bytes.begin(), bytes.end(), glyph.begin() + length);
    length = static_cast<std::uint8_t>(length + bytes.size());
    return true;
}

bool Cell::equals(const Cell& other) const {
    return kind == other.kind && wide == other.wide && length == other.length &&
           style.equals(other.style) &&
           std::equal(glyph.begin(), glyph.begin() + length, other.glyph.begin());
}

}  // namespace OpenAnimaTui::Screen
