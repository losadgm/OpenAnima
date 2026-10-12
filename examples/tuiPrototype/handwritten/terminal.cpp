#include "terminal.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace OpenAnimaPrototype {

namespace {

/// What the raw mode needs to remember and what has been read but not consumed.
struct State {
    termios saved{};
    bool savedValid{false};
    /// A read hands over as many bytes as happen to be waiting, and a paste or a fast
    /// typist hands over several: what is left waits here.
    std::string pending;
};

State& state() {
    static State instance;
    return instance;
}

/// Reads whatever is waiting into the pending buffer.
/// @param timeout How long to wait for the first byte.
/// @return True when there is at least one byte to consume.
bool fill(std::chrono::milliseconds timeout) {
    if (!state().pending.empty()) {
        return true;
    }

    pollfd descriptor{};
    descriptor.fd = STDIN_FILENO;
    descriptor.events = POLLIN;

    if (::poll(&descriptor, 1, static_cast<int>(timeout.count())) <= 0) {
        return false;
    }

    std::array<char, 64> buffer{};
    const ssize_t count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
    if (count <= 0) {
        return false;
    }
    state().pending.append(buffer.data(), static_cast<std::size_t>(count));
    return true;
}

}  // namespace

RawMode::RawMode() {
    if (isatty(STDIN_FILENO) == 0) {
        throw std::runtime_error("this needs a terminal");
    }
    if (tcgetattr(STDIN_FILENO, &state().saved) != 0) {
        throw std::runtime_error(std::string{"tcgetattr: "} + std::strerror(errno));
    }
    state().savedValid = true;

    termios raw = state().saved;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        throw std::runtime_error(std::string{"tcsetattr: "} + std::strerror(errno));
    }
    m_active = true;
}

RawMode::~RawMode() {
    if (m_active && state().savedValid) {
        tcsetattr(STDIN_FILENO, TCSANOW, &state().saved);
    }
}

std::size_t terminalWidth() {
    winsize size{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) -- ioctl takes its arguments variadically.
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return size.ws_col;
    }
    return 80;
}

void write(const std::string& text) {
    ssize_t written = 0;
    while (written < static_cast<ssize_t>(text.size())) {
        const ssize_t chunk = ::write(STDOUT_FILENO, text.data() + written,
                                      text.size() - static_cast<std::size_t>(written));
        if (chunk <= 0) {
            return;
        }
        written += chunk;
    }
}

std::optional<Key> readKey(std::chrono::milliseconds timeout) {
    if (!fill(timeout)) {
        return std::nullopt;
    }

    const auto first = static_cast<unsigned char>(state().pending.front());
    state().pending.erase(0, 1);

    Key key;
    if (first == 0x1B) {
        // What follows decides: on its own it is the escape key, and with a sequence it is
        // an arrow or some other key this prototype does not use.
        if (state().pending.empty()) {
            (void)fill(std::chrono::milliseconds{30});
        }
        if (!state().pending.empty() &&
            (state().pending.front() == '[' || state().pending.front() == 'O')) {
            state().pending.erase(0, 1);
            while (!state().pending.empty()) {
                const auto byte = static_cast<unsigned char>(state().pending.front());
                state().pending.erase(0, 1);
                if (byte >= 0x40 && byte <= 0x7E) {
                    break;
                }
            }
            key.kind = Key::Kind::Unknown;
            return key;
        }
        key.kind = Key::Kind::Escape;
        return key;
    }
    if (first == 0x03) {
        key.kind = Key::Kind::CtrlC;
        return key;
    }
    if (first == 0x04) {
        key.kind = Key::Kind::CtrlD;
        return key;
    }
    if (first == '\r' || first == '\n') {
        key.kind = Key::Kind::Enter;
        return key;
    }
    if (first == 0x7F || first == 0x08) {
        key.kind = Key::Kind::Backspace;
        return key;
    }
    if (first >= 0x20 && first < 0x7F) {
        key.kind = Key::Kind::Character;
        key.character = static_cast<char>(first);
        return key;
    }
    key.kind = Key::Kind::Unknown;
    return key;
}

std::vector<std::string> wrap(const std::string& text, std::size_t width) {
    std::vector<std::string> lines;
    if (width == 0) {
        width = 1;
    }

    std::string line;
    std::string word;
    const auto flushWord = [&] {
        if (word.empty()) {
            return;
        }
        if (line.empty()) {
            line = word;
        } else if (line.size() + 1 + word.size() <= width) {
            line += ' ';
            line += word;
        } else {
            lines.push_back(line);
            line = word;
        }
        word.clear();
    };

    for (const char character : text) {
        if (character == '\n') {
            flushWord();
            lines.push_back(line);
            line.clear();
            continue;
        }
        if (character == ' ') {
            flushWord();
            continue;
        }
        word += character;
        if (word.size() >= width) {
            flushWord();
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
        }
    }
    flushWord();
    if (!line.empty() || lines.empty()) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace OpenAnimaPrototype
