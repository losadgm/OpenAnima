#include "terminal.h"

#if !defined(_WIN32)

#include <array>
#include <cerrno>
#include <cstring>
#include <exception>
#include <fcntl.h>
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

        // Writing never waits: a terminal that has stopped reading must not be able to freeze the
        // client, which would leave the keyboard unread until it caught up.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        m_savedOutputFlags = fcntl(STDOUT_FILENO, F_GETFL, 0);
        if (m_savedOutputFlags >= 0) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            (void)fcntl(STDOUT_FILENO, F_SETFL, m_savedOutputFlags | O_NONBLOCK);
        }
    }

    ~UnixTerminal() override {
        if (m_active) {
            (void)flush();
            if (m_savedOutputFlags >= 0) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
                (void)fcntl(STDOUT_FILENO, F_SETFL, m_savedOutputFlags);
            }
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

    [[nodiscard]] std::size_t height() const override {
        winsize size{};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0) {
            return size.ws_row;
        }
        return 24;
    }

    std::optional<Key> readKey(std::chrono::milliseconds timeout) override {
        if (!m_utf8.empty()) {
            return nextUtf8Byte();
        }
        if (!fill(timeout)) {
            return std::nullopt;
        }

        const auto first = static_cast<unsigned char>(m_pending.front());
        m_pending.erase(0, 1);
        return decode(first);
    }

    void write(std::string_view text) override {
        m_out.append(text);
        (void)flush();
    }

    bool flush() override {
        while (!m_out.empty()) {
            const ssize_t chunk = ::write(STDOUT_FILENO, m_out.data(), m_out.size());
            if (chunk > 0) {
                m_out.erase(0, static_cast<std::size_t>(chunk));
                continue;
            }
            if (chunk < 0 && errno == EINTR) {
                continue;
            }
            // A terminal that is not reading fast enough, or that has gone away. Either way what
            // is left stays here and the client keeps reading the keyboard.
            return false;
        }
        return true;
    }

    [[nodiscard]] bool hasPending() const override {
        return !m_out.empty();
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
            if (introducer == '[' && !m_pending.empty() && m_pending.front() == 'M') {
                // The older mouse report: three bytes that carry a button and a position.
                return x10Mouse();
            }
            return sequenceKey(consumeSequence());
        }

        // Some other escape combination: drop the first byte of it and move on.
        m_pending.erase(0, 1);
        return Key{Key::Kind::Unknown, static_cast<char>(0)};
    }

    /// Reads the three bytes of an X10 mouse report, the `M` already consumed.
    ///
    /// Terminals only send it when they do not know the SGR form, which is asked for on entry,
    /// but an older emulator is not a reason for the wheel to do nothing.
    Key x10Mouse() {
        const std::size_t needed = 3;
        while (m_pending.size() < needed) {
            if (!fill(std::chrono::milliseconds{30}) || m_pending.size() < needed) {
                break;
            }
        }
        if (m_pending.size() < needed) {
            m_pending.clear();
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
        const auto button = static_cast<unsigned char>(m_pending[0]) - 32;
        m_pending.erase(0, needed);
        return wheelKey(button);
    }

    /// Waits for the rest of a sequence that has begun.
    /// @param needed Bytes the sequence still needs.
    /// @return Whether they arrived.
    bool waitFor(std::size_t needed) {
        while (m_pending.size() < needed) {
            if (!fill(std::chrono::milliseconds{30}) || m_pending.size() < needed) {
                return false;
            }
        }
        return true;
    }

    /// Consumes a CSI sequence and returns its bytes, final byte included.
    std::string consumeSequence() {
        std::string sequence;
        while (!m_pending.empty()) {
            const auto byte = static_cast<unsigned char>(m_pending.front());
            m_pending.erase(0, 1);
            sequence += static_cast<char>(byte);
            if (byte >= 0x40 && byte <= 0x7E) {
                break;
            }
        }
        return sequence;
    }

    /// Maps a cursor sequence to the key it spells.
    Key sequenceKey(const std::string& sequence) {
        if (sequence == "A") {
            return Key{Key::Kind::ArrowUp, static_cast<char>(0)};
        }
        if (sequence == "B") {
            return Key{Key::Kind::ArrowDown, static_cast<char>(0)};
        }
        if (sequence == "C") {
            return Key{Key::Kind::ArrowRight, static_cast<char>(0)};
        }
        if (sequence == "D") {
            return Key{Key::Kind::ArrowLeft, static_cast<char>(0)};
        }
        if (sequence == "5~") {
            return Key{Key::Kind::PageUp, static_cast<char>(0)};
        }
        if (sequence == "6~") {
            return Key{Key::Kind::PageDown, static_cast<char>(0)};
        }
        // Control and Return together, as the kitty protocol and xterm spell it.
        if (sequence == "13;5u" || sequence == "27;5;13~") {
            return Key{Key::Kind::CtrlEnter, static_cast<char>(0)};
        }
        // The enhanced keyboard protocol: a key is its code point, then its modifiers, then the
        // kind of event. Shift and Return together is a new line, and so is Control and Return.
        if (!sequence.empty() && sequence.back() == 'u') {
            return kittyKey(sequence.substr(0, sequence.size() - 1));
        }
        // A mouse report: `<button>;<column>;<row>` and an `M` or an `m` for a release.
        if (!sequence.empty() && sequence.front() == '<') {
            const std::size_t first = sequence.find(';');
            if (first == std::string::npos) {
                return Key{Key::Kind::Unknown, static_cast<char>(0)};
            }
            const std::string button = sequence.substr(1, first - 1);
            try {
                return wheelKey(static_cast<unsigned char>(std::stoi(button)));
            } catch (const std::exception&) {
                return Key{Key::Kind::Unknown, static_cast<char>(0)};
            }
        }
        // The start of a paste, which the terminal marks and then hands over as bytes.
        if (sequence == "200~") {
            return paste();
        }
        return Key{Key::Kind::Unknown, static_cast<char>(0)};
    }

    /// Maps a mouse button code to a key, which is the wheel or nothing at all.
    ///
    /// The clicks are not the client's business yet, so anything that is not a wheel notch is
    /// ignored rather than turned into a key nothing does.
    /// @param button Button code, the modifier bits included.
    /// @return The key the report stands for.
    static Key wheelKey(unsigned char button) {
        constexpr unsigned char WheelUp = 64;
        constexpr unsigned char WheelDown = 65;
        switch (button & 0x7FU) {
        case WheelUp:
            return Key{Key::Kind::WheelUp, static_cast<char>(0)};
        case WheelDown:
            return Key{Key::Kind::WheelDown, static_cast<char>(0)};
        default:
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
    }

    /// Maps a key of the enhanced keyboard protocol to the key it is.
    ///
    /// The form is `<code point>;<modifiers>[:event]`, where the modifiers are a bit mask: one
    /// is shift, two is alt, four is control and eight is the super key, with one added to the
    /// lot. Return with any of shift, alt or control is a new line, which is the whole reason
    /// the protocol is asked for: without it those chords are indistinguishable from a plain
    /// Return, a Tab or an Escape.
    /// @param parameters The sequence without its final byte.
    /// @return The key it spells.
    Key kittyKey(const std::string& parameters) {
        const std::size_t separator = parameters.find(';');
        if (separator == std::string::npos) {
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
        const std::string code = parameters.substr(0, separator);
        std::string modifiers = parameters.substr(separator + 1);
        // The event kind comes after a colon: `1` is a press, which is the only one acted on.
        const std::size_t colon = modifiers.find(':');
        std::string kind;
        if (colon != std::string::npos) {
            kind = modifiers.substr(colon + 1);
            modifiers = modifiers.substr(0, colon);
        }
        if (!kind.empty() && kind != "1") {
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }

        unsigned long mask = 1;
        unsigned long codepoint = 0;
        try {
            codepoint = std::stoul(code);
            mask = std::stoul(modifiers);
        } catch (const std::exception&) {
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
        // The modifiers carry a one that has to come off before the bits mean anything: shift is
        // one, alt is two, control is four, the super key is eight.
        const unsigned long bits = mask > 0 ? mask - 1 : 0;
        const bool shift = (bits & 1UL) != 0;
        const bool control = (bits & 4UL) != 0;

        if (codepoint == 13 || codepoint == 10) {
            return Key{(control || shift) ? Key::Kind::CtrlEnter : Key::Kind::Enter,
                       static_cast<char>(0)};
        }
        if (codepoint == 9) {
            return Key{shift ? Key::Kind::Unknown : Key::Kind::Tab, static_cast<char>(0)};
        }
        if (codepoint == 27) {
            return Key{Key::Kind::Escape, static_cast<char>(0)};
        }
        if (codepoint == 127) {
            return Key{Key::Kind::Backspace, static_cast<char>(0)};
        }
        if (codepoint < 0x20 || codepoint > 0x10FFFF) {
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
        if (control) {
            // Control and a letter is the byte the classic encoding would send.
            const auto lowercase =
                static_cast<unsigned long>(std::tolower(static_cast<unsigned char>(codepoint)));
            if (lowercase >= 'a' && lowercase <= 'z') {
                return Key{controlOrCharacter(static_cast<unsigned char>(lowercase - 'a' + 1)),
                           static_cast<char>(0)};
            }
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
        if (codepoint < 0x80) {
            return Key{Key::Kind::Character, static_cast<char>(codepoint)};
        }
        // Anything above ASCII arrives as its bytes, one key each, the way they do from an
        // ordinary terminal.
        const std::string bytes = encodeUtf8(codepoint);
        m_utf8.insert(m_utf8.end(), bytes.begin(), bytes.end());
        return nextUtf8Byte();
    }

    /// @param codepoint Code point to write.
    /// @return Its UTF-8 bytes.
    static std::string encodeUtf8(unsigned long codepoint) {
        std::string bytes;
        if (codepoint < 0x80) {
            bytes += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            bytes += static_cast<char>(0xC0U | (codepoint >> 6U));
            bytes += static_cast<char>(0x80U | (codepoint & 0x3FU));
        } else if (codepoint < 0x10000) {
            bytes += static_cast<char>(0xE0U | (codepoint >> 12U));
            bytes += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
            bytes += static_cast<char>(0x80U | (codepoint & 0x3FU));
        } else {
            bytes += static_cast<char>(0xF0U | (codepoint >> 18U));
            bytes += static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU));
            bytes += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
            bytes += static_cast<char>(0x80U | (codepoint & 0x3FU));
        }
        return bytes;
    }

    /// @return The next byte of the character being typed, UTF-8 bytes one key at a time.
    Key nextUtf8Byte() {
        const auto first = static_cast<unsigned char>(m_utf8.front());
        m_utf8.erase(0, 1);
        return Key{Key::Kind::Character, static_cast<char>(first)};
    }

    /// Reads a paste, which the terminal hands over as one block between two markers.
    ///
    /// The bytes are handed over as characters, and the line breaks inside become new lines in
    /// the draft rather than sends: pasting five lines is not five questions.
    /// @return The first key of the paste, or an unknown key when it never ended.
    Key paste() {
        while (true) {
            if (m_pending.empty() && !fill(std::chrono::milliseconds{500})) {
                m_pasted.clear();
                return Key{Key::Kind::Unknown, static_cast<char>(0)};
            }
            const std::size_t end = m_pending.find("\x1b[201~");
            if (end != std::string::npos) {
                m_pasted += m_pending.substr(0, end);
                m_pending.erase(0, end + 6);
                break;
            }
            m_pasted += m_pending;
            m_pending.clear();
        }

        std::string insertion;
        for (std::size_t offset = 0; offset < m_pasted.size(); ++offset) {
            const char value = m_pasted[offset];
            if (value == '\r') {
                // A line ending of either kind becomes the newline the draft knows.
                if (offset + 1 < m_pasted.size() && m_pasted[offset + 1] == '\n') {
                    ++offset;
                }
                insertion += '\n';
                continue;
            }
            insertion += value;
        }
        m_pasted.clear();
        m_utf8 = insertion + m_utf8;
        if (m_utf8.empty()) {
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
        return nextUtf8Byte();
    }

    /// Maps a control or printable byte to its key kind.
    static Key::Kind controlOrCharacter(unsigned char first) {
        // Everything above ASCII is a byte of a character the terminal sent as UTF-8, which is
        // what an accent, a tilde and an enye are. Dropping it is how typing in Spanish stopped
        // at the keyboard.
        if (first >= 0x20 && first != 0x7F) {
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
    int m_savedOutputFlags{0};
    /// Bytes read but not turned into keys yet: a read hands over everything that is
    /// waiting, and a paste or a fast typist hands over several.
    std::string m_pending;
    /// Bytes of a character that arrived whole, which are handed over one at a time so the rest
    /// of the client keeps seeing what a keyboard produces.
    std::string m_utf8;
    /// Text of the paste being read.
    std::string m_pasted;
    /// Bytes the terminal has not taken yet, which is why writing never waits for it.
    std::string m_out;
};

}  // namespace

std::unique_ptr<Terminal> makeTerminal() {
    return std::make_unique<UnixTerminal>();
}

}  // namespace OpenAnimaTui

#endif  // !defined(_WIN32)
