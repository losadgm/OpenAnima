#include "terminal.h"

#if !defined(_WIN32)

#include <array>
#include <cerrno>
#include <cstring>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace OpenAnimaTui {

namespace {

/// One terminal over a POSIX tty, in raw mode for as long as it lives.
class UnixTerminal final : public Terminal {
public:
    /// @throws std::runtime_error when standard input is not a terminal or it cannot be
    /// configured.
    UnixTerminal() {
        if (isatty(STDIN_FILENO) == 0) {
            throw std::runtime_error("this needs a terminal");
        }
        if (tcgetattr(STDIN_FILENO, &m_saved) != 0) {
            throw std::runtime_error(std::string{"tcgetattr: "} + std::strerror(errno));
        }

        termios raw = m_saved;
        // Raw input, not raw output: the newline on output still has to become CR LF, and
        // Ctrl+C has to arrive as a byte, not as a signal.
        raw.c_iflag &= static_cast<tcflag_t>(~(ICRNL | IXON));
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG | IEXTEN));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
            throw std::runtime_error(std::string{"tcsetattr: "} + std::strerror(errno));
        }
        m_active = true;
    }

    ~UnixTerminal() override {
        if (m_active) {
            tcsetattr(STDIN_FILENO, TCSANOW, &m_saved);
        }
    }

    UnixTerminal(const UnixTerminal&) = delete;
    UnixTerminal& operator=(const UnixTerminal&) = delete;
    UnixTerminal(UnixTerminal&&) = delete;
    UnixTerminal& operator=(UnixTerminal&&) = delete;

    [[nodiscard]] std::size_t width() const override {
        winsize size{};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
            return size.ws_col;
        }
        return 80;
    }

    std::optional<Key> readKey(std::chrono::milliseconds timeout) override {
        if (!fill(timeout)) {
            return std::nullopt;
        }

        const auto first = static_cast<unsigned char>(m_pending.front());
        m_pending.erase(0, 1);
        return decode(first);
    }

    void write(std::string_view text) override {
        std::size_t written = 0;
        while (written < text.size()) {
            const ssize_t chunk =
                ::write(STDOUT_FILENO, text.data() + written, text.size() - written);
            if (chunk <= 0) {
                return;
            }
            written += static_cast<std::size_t>(chunk);
        }
    }

private:
    /// Reads whatever is waiting into the pending buffer.
    /// @param timeout How long to wait for the first byte.
    /// @return True when there is at least one byte to consume.
    bool fill(std::chrono::milliseconds timeout) {
        if (!m_pending.empty()) {
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
        m_pending.append(buffer.data(), static_cast<std::size_t>(count));
        return true;
    }

    /// Turns the first byte of a press into a key, resolving the sequences that begin with
    /// an escape.
    Key decode(unsigned char first) {
        Key key;

        if (first == 0x1B) {
            return decodeEscape();
        }

        key.kind = controlOrCharacter(first);
        key.character =
            key.kind == Key::Kind::Character ? static_cast<char>(first) : static_cast<char>(0);
        return key;
    }

    /// Reads the rest of an escape sequence, or calls the byte an Escape key.
    Key decodeEscape() {
        // An escape on its own is the Escape key; followed by a sequence it is an arrow or
        // some other key this client does not use. Wait a moment for the rest to arrive.
        if (m_pending.empty()) {
            (void)fill(std::chrono::milliseconds{30});
        }
        if (m_pending.empty()) {
            return Key{Key::Kind::Escape, static_cast<char>(0)};
        }

        const char introducer = m_pending.front();
        if (introducer == '[' || introducer == 'O') {
            m_pending.erase(0, 1);
            const char final = consumeSequence();
            return arrow(final);
        }

        // Some other escape combination: drop the first byte of it and move on.
        m_pending.erase(0, 1);
        return Key{Key::Kind::Unknown, static_cast<char>(0)};
    }

    /// Consumes a CSI sequence and returns its final byte.
    char consumeSequence() {
        while (!m_pending.empty()) {
            const auto byte = static_cast<unsigned char>(m_pending.front());
            m_pending.erase(0, 1);
            if (byte >= 0x40 && byte <= 0x7E) {
                return static_cast<char>(byte);
            }
        }
        return 0;
    }

    /// Maps the final byte of a cursor sequence to an arrow key.
    static Key arrow(char final) {
        switch (final) {
        case 'A':
            return Key{Key::Kind::ArrowUp, static_cast<char>(0)};
        case 'B':
            return Key{Key::Kind::ArrowDown, static_cast<char>(0)};
        case 'C':
            return Key{Key::Kind::ArrowRight, static_cast<char>(0)};
        case 'D':
            return Key{Key::Kind::ArrowLeft, static_cast<char>(0)};
        default:
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
    }

    /// Maps a control or printable byte to its key kind.
    static Key::Kind controlOrCharacter(unsigned char first) {
        if (first >= 0x20 && first < 0x7F) {
            return Key::Kind::Character;
        }
        switch (first) {
        case 0x0D:  // Return.
            return Key::Kind::Enter;
        case 0x0A:  // Ctrl+J, the newline without the return.
            return Key::Kind::CtrlJ;
        case 0x09:  // Tab.
            return Key::Kind::Tab;
        case 0x7F:  // Backspace (Del on some terminals).
        case 0x08:  // Ctrl+H.
            return Key::Kind::Backspace;
        case 0x03:
            return Key::Kind::CtrlC;
        case 0x04:
            return Key::Kind::CtrlD;
        case 0x01:
            return Key::Kind::CtrlA;
        case 0x02:
            return Key::Kind::CtrlB;
        case 0x05:
            return Key::Kind::CtrlE;
        case 0x06:
            return Key::Kind::CtrlF;
        case 0x0E:
            return Key::Kind::CtrlN;
        case 0x10:
            return Key::Kind::CtrlP;
        default:
            return Key::Kind::Unknown;
        }
    }

    termios m_saved{};
    bool m_active{false};
    /// Bytes read but not turned into keys yet: a read hands over everything that is
    /// waiting, and a paste or a fast typist hands over several.
    std::string m_pending;
};

}  // namespace

std::unique_ptr<Terminal> makeTerminal() {
    return std::make_unique<UnixTerminal>();
}

}  // namespace OpenAnimaTui

#endif  // !defined(_WIN32)
