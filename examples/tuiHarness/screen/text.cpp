#include "text.h"

#include <string>

namespace OpenAnimaTui::Screen {

std::size_t utf8Length(unsigned char byte) {
    if (byte < 0x80) {
        return 1;
    }
    if ((byte >> 5) == 0x6) {
        return 2;
    }
    if ((byte >> 4) == 0xE) {
        return 3;
    }
    if ((byte >> 3) == 0x1E) {
        return 4;
    }
    return 1;
}

std::size_t visibleColumns(const std::string& text) {
    std::size_t columns = 0;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '\x1b' && index + 1 < text.size() && text[index + 1] == '[') {
            index += 2;
            while (index < text.size() && text[index] != 'm') {
                ++index;
            }
            if (index < text.size()) {
                ++index;
            }
            continue;
        }
        index += utf8Length(static_cast<unsigned char>(text[index]));
        ++columns;
    }
    return columns;
}

std::string padRow(const std::string& row, std::size_t width) {
    const std::size_t columns = visibleColumns(row);
    if (columns == width) {
        return row;
    }
    if (columns < width) {
        return row + std::string(width - columns, ' ');
    }

    // A row that is too long is cut, never left to wrap on its own: a wrapped row would push
    // every row below it down and leave the caret pointing at the wrong line.
    std::string cut;
    std::size_t kept = 0;
    for (std::size_t index = 0; index < row.size() && kept < width;) {
        if (row[index] == '\x1b' && index + 1 < row.size() && row[index + 1] == '[') {
            const std::size_t end = row.find('m', index + 2);
            const std::size_t length =
                end == std::string::npos ? row.size() - index : end - index + 1;
            cut.append(row, index, length);
            index += length;
            continue;
        }
        const std::size_t length = utf8Length(static_cast<unsigned char>(row[index]));
        cut.append(row, index, length);
        index += length;
        ++kept;
    }
    return cut;
}

std::vector<std::string> wrap(const std::string& text, std::size_t width) {
    std::vector<std::string> lines;
    if (width == 0) {
        width = 1;
    }

    std::string line;
    std::size_t lineColumns = 0;
    std::string word;
    std::size_t wordColumns = 0;

    const auto flushWord = [&] {
        if (word.empty()) {
            return;
        }
        if (line.empty()) {
            line = word;
            lineColumns = wordColumns;
        } else if (lineColumns + 1 + wordColumns <= width) {
            line += ' ';
            line += word;
            lineColumns += 1 + wordColumns;
        } else {
            lines.push_back(line);
            line = word;
            lineColumns = wordColumns;
        }
        word.clear();
        wordColumns = 0;
    };

    for (std::size_t index = 0; index < text.size();) {
        const std::size_t length = utf8Length(static_cast<unsigned char>(text[index]));
        const std::string codepoint = text.substr(index, length);
        if (codepoint == "\n") {
            flushWord();
            lines.push_back(line);
            line.clear();
            lineColumns = 0;
        } else if (codepoint == " ") {
            flushWord();
        } else {
            word += codepoint;
            ++wordColumns;
            if (wordColumns >= width) {
                flushWord();
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                    lineColumns = 0;
                }
            }
        }
        index += length;
    }
    flushWord();
    if (!line.empty() || lines.empty()) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace OpenAnimaTui::Screen
