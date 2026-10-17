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

    void copyToClipboard(std::string_view text) override {
        // OSC 52: the text travels to the terminal, which puts it in the clipboard of whoever is
        // looking at the screen. It is the only way that works over ssh with nothing installed at
        // the other end.
        if (text.size() > MaxClipboardBytes) {
            return;
        }
        write("\x1b]52;c;");
        write(base64(text));
        write("\x07");
        (void)flush();
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
        if (first == 0x1B) {
            return decodeEscape();
        }
        return keyOfByte(first);
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
        // Alt and backspace together is the word before the caret, the way readline spells it.
        if (introducer == '\x7f' || introducer == '\b') {
            m_pending.erase(0, 1);
            return controlled(Key::Kind::Backspace);
        }
        // Alt and `d` is the word after the caret in readline, and it is what an editor that
        // takes the chord before the terminal sees it sends for control and delete: VS Code
        // maps that chord to `ESC d` itself.
        if (introducer == 'd') {
            m_pending.erase(0, 1);
            return controlled(Key::Kind::Delete);
        }
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
    /// @param kind What was pressed.
    /// @return The key, with control held.
    [[nodiscard]] static Key controlled(Key::Kind kind) {
        Key key{kind, static_cast<char>(0)};
        key.ctrl = true;
        return key;
    }

    Key sequenceKey(const std::string& rawSequence) {
        // The enhanced protocol can add the kind of event after a colon: `3;5:1~` is a Delete
        // with control being pressed, which is the same key as `3;5~`.
        std::string sequence = rawSequence;
        if (!sequence.empty() && sequence.back() == '~') {
            const std::size_t colon = sequence.find(':');
            if (colon != std::string::npos) {
                sequence.erase(colon, sequence.size() - 1 - colon);
            }
        }

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
        if (sequence == "3~") {
            return Key{Key::Kind::Delete, static_cast<char>(0)};
        }
        if (sequence == "5~") {
            return Key{Key::Kind::PageUp, static_cast<char>(0)};
        }
        if (sequence == "6~") {
            return Key{Key::Kind::PageDown, static_cast<char>(0)};
        }
        // Control held with an arrow or with Delete: `;5` is the modifier part of the sequence,
        // with one added to the four that mean control.
        if (sequence == "1;5C") {
            return controlled(Key::Kind::ArrowRight);
        }
        if (sequence == "1;5D") {
            return controlled(Key::Kind::ArrowLeft);
        }
        // Control and Delete as the modifyOtherKeys encoding spells it, and as xterm spells the
        // key when it reports it itself: `27;5;3~` is a Delete with control held.
        if (sequence == "3;5~" || sequence == "27;5;3~") {
            return controlled(Key::Kind::Delete);
        }
        // Control and backspace as xterm spells it when it reports the key itself: 8 is the
        // code point of the byte the classic encoding sends for it.
        if (sequence == "27;5;8~" || sequence == "27;5;127~") {
            return controlled(Key::Kind::Backspace);
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
            return mouseKey(sequence);
        }
        // The start of a paste, which the terminal marks and then hands over as bytes.
        if (sequence == "200~") {
            return paste();
        }
        return Key{Key::Kind::Unknown, static_cast<char>(0)};
    }

    /// Maps a mouse report to the key it is: a wheel notch, a press, a drag or a release.
    ///
    /// @param sequence The report, without the introducer: `<button>;<column>;<row>` and an `M`
    /// for something happening or an `m` for a button coming back up.
    /// @return The key, with the position counted from zero.
    static Key mouseKey(const std::string& sequence) {
        const std::size_t first = sequence.find(';');
        const std::size_t second =
            first == std::string::npos ? std::string::npos : sequence.find(';', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
        const char final = sequence.back();
        try {
            const int button = std::stoi(sequence.substr(1, first - 1));
            const auto column = static_cast<std::size_t>(
                std::stoul(sequence.substr(first + 1, second - first - 1)));
            const auto row = static_cast<std::size_t>(std::stoul(sequence.substr(second + 1)));
            if (Key wheel = wheelKey(static_cast<unsigned char>(button));
                wheel.kind != Key::Kind::Unknown) {
                // A wheel report carries the cell it happened on, which is what tells the client
                // which of the two scrolls the user meant.
                wheel.column = column > 0 ? column - 1 : 0;
                wheel.row = row > 0 ? row - 1 : 0;
                return wheel;
            }

            Key key;
            // Bit five says the mouse is moving. With no button held that is the pointer going
            // over the screen, which is what a hover is; with the left button held it is a drag.
            const bool moving = (button & 32) != 0;
            const auto held = static_cast<unsigned char>(button & 3);
            if (moving) {
                key.kind = Key::Kind::MouseDrag;
                if (held == 3) {
                    key.kind = Key::Kind::MouseMove;  // No button down: the pointer passing over.
                } else if (held != 0) {
                    key.kind = Key::Kind::Unknown;  // The right or the middle button.
                }
            } else if (held != 0) {
                // The right and the middle button are not the client's: leaving them alone is
                // what lets the terminal or the editor show its own menu on a right click.
                key.kind = Key::Kind::Unknown;
            } else {
                key.kind = final == 'm' ? Key::Kind::MouseRelease : Key::Kind::MousePress;
            }
            key.column = column > 0 ? column - 1 : 0;
            key.row = row > 0 ? row - 1 : 0;
            return key;
        } catch (const std::exception&) {
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
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
    /// The form is `<code point>[:<alternate>][;<modifiers>[:event]]`, where the modifiers are a
    /// bit mask: one is shift, two is alt, four is control and eight is the super key, with one
    /// added to the lot. Both the alternate code point and the modifiers are optional, and a
    /// terminal that was asked to disambiguate sends a bare `CSI 27 u` for an Escape key.
    /// Return with any of shift, alt or control is a new line, which is the whole reason the
    /// protocol is asked for: without it those chords are indistinguishable from a plain
    /// Return, a Tab or an Escape.
    /// @param parameters The sequence without its final byte.
    /// @return The key it spells.
    Key kittyKey(const std::string& parameters) {
        const std::size_t separator = parameters.find(';');
        std::string code =
            separator == std::string::npos ? parameters : parameters.substr(0, separator);
        // The key can carry the code point it has on another layout after a colon.
        if (const std::size_t alternate = code.find(':'); alternate != std::string::npos) {
            code = code.substr(0, alternate);
        }
        // `1` is the mask of no modifier at all, which is what an absent one means.
        std::string modifiers =
            separator == std::string::npos ? std::string{"1"} : parameters.substr(separator + 1);
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
        return kittyCode(codepoint, (bits & 1UL) != 0, (bits & 4UL) != 0);
    }

    /// Maps a code point the enhanced keyboard protocol reported, its modifiers already read.
    /// @param codepoint Code point of the key.
    /// @param shift Whether shift was held.
    /// @param control Whether control was held.
    /// @return The key it spells.
    Key kittyCode(unsigned long codepoint, bool shift, bool control) {
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
            return control ? controlled(Key::Kind::Backspace)
                           : Key{Key::Kind::Backspace, static_cast<char>(0)};
        }
        // The key of the enhanced protocol that stands for Delete. With control it takes the word
        // after the caret; without it, the character, which is what the key does everywhere.
        if (codepoint == 3) {
            return control ? controlled(Key::Kind::Delete) : Key{Key::Kind::Delete, '\0'};
        }
        if (codepoint < 0x20 || codepoint > 0x10FFFF) {
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
        if (control) {
            // Control and a letter is the byte the classic encoding would send.
            const auto lowercase =
                static_cast<unsigned long>(std::tolower(static_cast<unsigned char>(codepoint)));
            if (lowercase >= 'a' && lowercase <= 'z') {
                return keyOfByte(static_cast<unsigned char>(lowercase - 'a' + 1));
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
    /// @param first Byte that arrived.
    /// @return The key it is, a printable one included.
    static Key keyOfByte(unsigned char first) {
        // Everything above ASCII is a byte of a character the terminal sent as UTF-8, which is
        // what an accent, a tilde and an enye are. Dropping it is how typing in Spanish stopped
        // at the keyboard.
        if (first >= 0x20 && first != 0x7F) {
            return Key{Key::Kind::Character, static_cast<char>(first)};
        }
        switch (first) {
        case 0x0D:  // Return.
            return Key{Key::Kind::Enter, static_cast<char>(0)};
        case 0x0A:  // Ctrl+J, the newline without the return.
            return Key{Key::Kind::CtrlJ, static_cast<char>(0)};
        case 0x09:  // Tab.
            return Key{Key::Kind::Tab, static_cast<char>(0)};
        case 0x7F:  // Backspace (Del on some terminals).
            return Key{Key::Kind::Backspace, static_cast<char>(0)};
        // 0x08 is Ctrl+H by the book, and it is what most terminals send when control and
        // backspace are pressed together: the two chords are the same byte, and in a line being
        // typed the useful reading of it is the one that takes a word. The 0x17 below is the
        // same key under another name, so the two branches answer alike on purpose.
        // NOLINTNEXTLINE(bugprone-branch-clone)
        case 0x08:
            return controlled(Key::Kind::Backspace);
        // 0x17 is Ctrl+W, which is what a terminal sends for control and backspace when it has
        // no other way to say it, and the word before the caret is what it means in readline.
        case 0x17:
            return controlled(Key::Kind::Backspace);
        case 0x03:
            return Key{Key::Kind::CtrlC, static_cast<char>(0)};
        case 0x04:
            return Key{Key::Kind::CtrlD, static_cast<char>(0)};
        case 0x01:
            return Key{Key::Kind::CtrlA, static_cast<char>(0)};
        case 0x02:
            return Key{Key::Kind::CtrlB, static_cast<char>(0)};
        case 0x05:
            return Key{Key::Kind::CtrlE, static_cast<char>(0)};
        case 0x06:
            return Key{Key::Kind::CtrlF, static_cast<char>(0)};
        case 0x0E:
            return Key{Key::Kind::CtrlN, static_cast<char>(0)};
        case 0x10:
            return Key{Key::Kind::CtrlP, static_cast<char>(0)};
        case 0x16:
            return Key{Key::Kind::CtrlV, static_cast<char>(0)};
        default:
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
    }

    /// @param text Text to encode.
    /// @return The text in base64, which is how the clipboard sequence carries it.
    static std::string base64(std::string_view text) {
        static constexpr std::string_view Alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        encoded.reserve(((text.size() + 2) / 3) * 4);
        std::size_t offset = 0;
        while (offset + 2 < text.size()) {
            const auto first = static_cast<unsigned char>(text[offset]);
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            const auto third = static_cast<unsigned char>(text[offset + 2]);
            const std::uint32_t group = (static_cast<std::uint32_t>(first) << 16U) |
                                        (static_cast<std::uint32_t>(second) << 8U) |
                                        static_cast<std::uint32_t>(third);
            encoded += Alphabet.at((group >> 18U) & 0x3FU);
            encoded += Alphabet.at((group >> 12U) & 0x3FU);
            encoded += Alphabet.at((group >> 6U) & 0x3FU);
            encoded += Alphabet.at(group & 0x3FU);
            offset += 3;
        }
        const std::size_t left = text.size() - offset;
        if (left == 1) {
            const auto first = static_cast<unsigned char>(text[offset]);
            const std::uint32_t group = static_cast<std::uint32_t>(first) << 16U;
            encoded += Alphabet.at((group >> 18U) & 0x3FU);
            encoded += Alphabet.at((group >> 12U) & 0x3FU);
            encoded += "==";
        } else if (left == 2) {
            const auto first = static_cast<unsigned char>(text[offset]);
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            const std::uint32_t group = (static_cast<std::uint32_t>(first) << 16U) |
                                        (static_cast<std::uint32_t>(second) << 8U);
            encoded += Alphabet.at((group >> 18U) & 0x3FU);
            encoded += Alphabet.at((group >> 12U) & 0x3FU);
            encoded += Alphabet.at((group >> 6U) & 0x3FU);
            encoded += '=';
        }
        return encoded;
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

    /// The clipboard sequence carries the text inline, and terminals cut it off at some length;
    /// a region longer than this is not worth a sequence that will be dropped.
    static constexpr std::size_t MaxClipboardBytes = std::size_t{74} * 1024;
};

}  // namespace

std::unique_ptr<Terminal> makeTerminal() {
    return std::make_unique<UnixTerminal>();
}

}  // namespace OpenAnimaTui

#endif  // !defined(_WIN32)
