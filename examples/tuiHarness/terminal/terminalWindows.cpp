#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <windows.h>

#include "terminal.h"

namespace OpenAnimaTui {

namespace {

/// One terminal over the Windows console, in raw mode for as long as it lives.
///
/// The console is put into the mode a line editor needs: no echo, no line buffering and
/// no Ctrl+C handling, with virtual terminal sequences enabled on output so the ANSI
/// colours and cursor moves the screen layer emits pass through.
class WindowsTerminal final : public Terminal {
public:
    /// @throws std::runtime_error when standard input is not a console or it cannot be
    /// configured.
    WindowsTerminal() {
        m_input = GetStdHandle(STD_INPUT_HANDLE);
        m_output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (GetFileType(m_input) != FILE_TYPE_CHAR) {
            throw std::runtime_error("this needs a terminal");
        }

        DWORD inputMode = 0;
        if (GetConsoleMode(m_input, &inputMode) == 0) {
            throw std::runtime_error("GetConsoleMode failed");
        }
        m_savedInputMode = inputMode;

        const DWORD rawInput =
            inputMode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                                            ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
        // The console keeps the mouse to itself while quick edit is on, and the button flags are
        // only read when the extended flags are asked for.
        if (SetConsoleMode(m_input, rawInput | ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT) == 0) {
            throw std::runtime_error("SetConsoleMode failed");
        }

        // Best effort: ANSI output needs virtual terminal processing, which the console
        // gets without it too, only without the colours.
        DWORD outputMode = 0;
        if (GetConsoleMode(m_output, &outputMode) != 0) {
            m_savedOutputMode = outputMode;
            (void)SetConsoleMode(m_output, outputMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }

        // The client writes UTF-8 and the console reads what it is handed with its own code
        // page: under the one a legacy console starts with, an enye arrives as two characters.
        m_savedOutputCodePage = GetConsoleOutputCP();
        (void)SetConsoleOutputCP(CP_UTF8);
    }

    ~WindowsTerminal() override {
        if (m_savedOutputCodePage != 0) {
            (void)SetConsoleOutputCP(m_savedOutputCodePage);
        }
        (void)SetConsoleMode(m_input, m_savedInputMode);
        (void)SetConsoleMode(m_output, m_savedOutputMode);
    }

    WindowsTerminal(const WindowsTerminal&) = delete;
    WindowsTerminal& operator=(const WindowsTerminal&) = delete;
    WindowsTerminal(WindowsTerminal&&) = delete;
    WindowsTerminal& operator=(WindowsTerminal&&) = delete;

    std::size_t width() const override {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(m_output, &info) != 0) {
            return static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
        }
        return 80;
    }

    std::size_t height() const override {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(m_output, &info) != 0) {
            return static_cast<std::size_t>(info.srWindow.Bottom - info.srWindow.Top + 1);
        }
        return 24;
    }

    std::optional<Key> readKey(std::chrono::milliseconds timeout) override {
        if (!m_pending.empty()) {
            const auto first = static_cast<unsigned char>(m_pending.front());
            m_pending.erase(0, 1);
            return character(first);
        }

        const DWORD wait = static_cast<DWORD>(timeout.count());
        const DWORD result = WaitForSingleObject(m_input, wait);
        if (result != WAIT_OBJECT_0) {
            return std::nullopt;
        }

        while (true) {
            INPUT_RECORD record{};
            DWORD read = 0;
            if (ReadConsoleInputW(m_input, &record, 1, &read) == 0 || read == 0) {
                return std::nullopt;
            }
            if (record.EventType == MOUSE_EVENT) {
                if (const auto mouse = translateMouse(record.Event.MouseEvent)) {
                    return mouse;
                }
                continue;
            }
            if (record.EventType != KEY_EVENT || record.Event.KeyEvent.bKeyDown == FALSE) {
                continue;
            }
            return translate(record.Event.KeyEvent);
        }
    }

    void write(std::string_view text) override {
        if (text.empty()) {
            return;
        }
        // The console API takes a string at a time, so what does not fit waits here rather than
        // holding up the loop that reads the keyboard.
        m_out.append(text);
        (void)flush();
    }

    bool flush() override {
        while (!m_out.empty()) {
            const int written =
                _write(_fileno(stdout), m_out.data(), static_cast<unsigned>(m_out.size()));
            if (written <= 0) {
                return false;
            }
            m_out.erase(0, static_cast<std::size_t>(written));
        }
        return true;
    }

    [[nodiscard]] bool hasPending() const override {
        return !m_out.empty();
    }

    /// Puts a text in the Windows clipboard, which is the one the user can paste from
    /// everywhere else.
    /// @param text Text to copy, which is dropped when there is nothing to put.
    void copyToClipboard(std::string_view text) override {
        const std::wstring wide = wideOf(text);
        if (wide.empty()) {
            return;
        }
        if (OpenClipboard(nullptr) == 0) {
            return;
        }
        if (EmptyClipboard() == 0) {
            (void)CloseClipboard();
            return;
        }
        // The clipboard takes the memory and frees it, which is why it has to be movable and why
        // it is ours to free when it is refused.
        const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory == nullptr) {
            (void)CloseClipboard();
            return;
        }
        void* target = GlobalLock(memory);
        if (target == nullptr) {
            (void)GlobalFree(memory);
            (void)CloseClipboard();
            return;
        }
        std::memcpy(target, wide.c_str(), bytes);
        (void)GlobalUnlock(memory);
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
            (void)GlobalFree(memory);
        }
        (void)CloseClipboard();
    }

private:
    /// Turns a mouse event into a key: the wheel, or a press, a drag and a release with where
    /// they happened.
    /// @param record Mouse event the console reported.
    /// @return The key, or nothing when the event is not one the client uses.
    [[nodiscard]] static std::optional<Key> translateMouse(const MOUSE_EVENT_RECORD& record) {
        if (record.dwEventFlags == MOUSE_WHEELED) {
            // The distance is in the high word, and its sign is the direction.
            const auto delta = static_cast<SHORT>(HIWORD(record.dwButtonState));
            if (delta > 0) {
                return Key{Key::Kind::WheelUp, static_cast<char>(0)};
            }
            if (delta < 0) {
                return Key{Key::Kind::WheelDown, static_cast<char>(0)};
            }
            return std::nullopt;
        }
        if (record.dwEventFlags == MOUSE_MOVED) {
            // Motion arrives whether or not a button is down, and only a button being held is a
            // drag: otherwise the picking out would follow the pointer around the screen.
            constexpr DWORD AnyButton = FROM_LEFT_1ST_BUTTON_PRESSED |
                                        FROM_LEFT_2ND_BUTTON_PRESSED |
                                        FROM_LEFT_3RD_BUTTON_PRESSED |
                                        FROM_LEFT_4TH_BUTTON_PRESSED | RIGHTMOST_BUTTON_PRESSED;
            if ((record.dwButtonState & AnyButton) == 0) {
                return std::nullopt;
            }
            return positioned(Key::Kind::MouseDrag, record);
        }
        if (record.dwEventFlags == MOUSE_HWHEELED || record.dwEventFlags == DOUBLE_CLICK) {
            return std::nullopt;
        }
        if ((record.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0) {
            return positioned(Key::Kind::MousePress, record);
        }
        return positioned(Key::Kind::MouseRelease, record);
    }

    /// Adds where a mouse event happened to the key it is.
    /// @param kind What happened.
    /// @param record Mouse event the console reported.
    /// @return The key, with the position counted from zero.
    [[nodiscard]] static Key positioned(const Key::Kind kind, const MOUSE_EVENT_RECORD& record) {
        Key key;
        key.kind = kind;
        key.column =
            record.dwMousePosition.X >= 0 ? static_cast<std::size_t>(record.dwMousePosition.X) : 0;
        key.row =
            record.dwMousePosition.Y >= 0 ? static_cast<std::size_t>(record.dwMousePosition.Y) : 0;
        return key;
    }

    /// @param kind What was pressed.
    /// @param control Whether control was held.
    /// @return The key, with the flag set, which is what makes an arrow or a delete move by
    ///         words.
    [[nodiscard]] static Key plain(const Key::Kind kind, const bool control) {
        Key key{kind, static_cast<char>(0)};
        key.ctrl = control;
        return key;
    }

    /// Bytes the console has not taken yet.
    std::string m_out;
    /// Turns one key event into a key, spelling printable characters as UTF-8 bytes in the
    /// pending buffer, one byte per press, the way the Unix side delivers them.
    std::optional<Key> translate(const KEY_EVENT_RECORD& record) {
        const bool control =
            (record.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

        if (control && record.wVirtualKeyCode >= 'A' && record.wVirtualKeyCode <= 'Z') {
            const unsigned char controlByte =
                static_cast<unsigned char>(record.wVirtualKeyCode - 'A' + 1);
            return controlKey(controlByte);
        }

        if (control && record.wVirtualKeyCode == VK_RETURN) {
            return Key{Key::Kind::CtrlEnter, static_cast<char>(0)};
        }

        switch (record.wVirtualKeyCode) {
        case VK_RETURN:
            return Key{Key::Kind::Enter, static_cast<char>(0)};
        case VK_BACK:
            return plain(Key::Kind::Backspace, control);
        case VK_DELETE:
            return plain(Key::Kind::Delete, control);
        case VK_TAB:
            return Key{Key::Kind::Tab, static_cast<char>(0)};
        case VK_ESCAPE:
            return Key{Key::Kind::Escape, static_cast<char>(0)};
        case VK_UP:
            return Key{Key::Kind::ArrowUp, static_cast<char>(0)};
        case VK_DOWN:
            return Key{Key::Kind::ArrowDown, static_cast<char>(0)};
        case VK_LEFT:
            return plain(Key::Kind::ArrowLeft, control);
        case VK_RIGHT:
            return plain(Key::Kind::ArrowRight, control);
        case VK_PRIOR:
            return Key{Key::Kind::PageUp, static_cast<char>(0)};
        case VK_NEXT:
            return Key{Key::Kind::PageDown, static_cast<char>(0)};
        default:
            break;
        }

        if (record.uChar.UnicodeChar != 0 && record.uChar.UnicodeChar >= 0x20) {
            const std::string bytes = utf8Of(record.uChar.UnicodeChar);
            if (!bytes.empty()) {
                m_pending = bytes;
                const auto first = static_cast<unsigned char>(m_pending.front());
                m_pending.erase(0, 1);
                return character(first);
            }
        }

        return Key{Key::Kind::Unknown, static_cast<char>(0)};
    }

    /// Maps a control byte (0x01..0x1A) to the key kind the client knows.
    static std::optional<Key> controlKey(unsigned char byte) {
        switch (byte) {
        case 0x0A:
            return Key{Key::Kind::CtrlJ, static_cast<char>(0)};
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
        default:
            return Key{Key::Kind::Unknown, static_cast<char>(0)};
        }
    }

    /// Builds a printable character key from one UTF-8 byte.
    ///
    /// Every byte is one, including the ones above ASCII: an accent and an enye are two of them,
    /// and the rest of the client puts them back together.
    /// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    static Key character(unsigned char byte) {
        return Key{Key::Kind::Character, static_cast<char>(byte)};
    }

    /// The UTF-8 bytes of one wide character, which is what the rest of the client reads.
    /// @param wide Character the console reported, in UTF-16.
    /// @return The bytes, empty when the console handed over nothing usable.
    [[nodiscard]] static std::string utf8Of(wchar_t wide) {
        if (wide < 0x20) {
            return {};
        }
        const wchar_t text[2] = {wide, L'\0'};
        char bytes[4] = {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, text, 1, bytes,
                                               static_cast<int>(sizeof(bytes)), nullptr, nullptr);
        return length > 0 ? std::string(bytes, bytes + length) : std::string{};
    }

    /// The UTF-16 form of a text, which is what the wide Windows calls take.
    /// @param text Text to convert, in UTF-8.
    /// @return The text, empty when there is nothing that converts.
    [[nodiscard]] static std::wstring wideOf(std::string_view text) {
        if (text.empty()) {
            return {};
        }
        const int length =
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0) {
            return {};
        }
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        (void)MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                  wide.data(), length);
        return wide;
    }

    HANDLE m_input{INVALID_HANDLE_VALUE};
    HANDLE m_output{INVALID_HANDLE_VALUE};
    DWORD m_savedInputMode{0};
    DWORD m_savedOutputMode{0};
    /// Output code page the console had, so it is handed back the way it was.
    UINT m_savedOutputCodePage{0};
    /// UTF-8 bytes of a wide character not yet handed over, one byte per readKey.
    std::string m_pending;
};

}  // namespace

std::unique_ptr<Terminal> makeTerminal() {
    return std::make_unique<WindowsTerminal>();
}

}  // namespace OpenAnimaTui

#endif  // defined(_WIN32)
