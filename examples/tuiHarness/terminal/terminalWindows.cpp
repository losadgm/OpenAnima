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
#include <vector>
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
            inputMode &
            ~static_cast<DWORD>(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        if (SetConsoleMode(m_input, rawInput | ENABLE_MOUSE_INPUT) == 0) {
            throw std::runtime_error("SetConsoleMode failed");
        }

        // Best effort: ANSI output needs virtual terminal processing, which the console
        // gets without it too, only without the colours.
        DWORD outputMode = 0;
        if (GetConsoleMode(m_output, &outputMode) != 0) {
            m_savedOutputMode = outputMode;
            (void)SetConsoleMode(m_output, outputMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }

    ~WindowsTerminal() override {
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
                if (const auto wheel = translateWheel(record.Event.MouseEvent)) {
                    return wheel;
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

private:
    /// Turns a wheel event into a key, and lets the clicks pass.
    /// @param record Mouse event the console reported.
    /// @return The key, or nothing when the event is not the wheel.
    [[nodiscard]] static std::optional<Key> translateWheel(const MOUSE_EVENT_RECORD& record) {
        if (record.dwEventFlags != MOUSE_WHEELED) {
            return std::nullopt;
        }
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
            return Key{Key::Kind::Backspace, static_cast<char>(0)};
        case VK_TAB:
            return Key{Key::Kind::Tab, static_cast<char>(0)};
        case VK_ESCAPE:
            return Key{Key::Kind::Escape, static_cast<char>(0)};
        case VK_UP:
            return Key{Key::Kind::ArrowUp, static_cast<char>(0)};
        case VK_DOWN:
            return Key{Key::Kind::ArrowDown, static_cast<char>(0)};
        case VK_LEFT:
            return Key{Key::Kind::ArrowLeft, static_cast<char>(0)};
        case VK_RIGHT:
            return Key{Key::Kind::ArrowRight, static_cast<char>(0)};
        case VK_PRIOR:
            return Key{Key::Kind::PageUp, static_cast<char>(0)};
        case VK_NEXT:
            return Key{Key::Kind::PageDown, static_cast<char>(0)};
        default:
            break;
        }

        if (record.uChar.UnicodeChar != 0 && record.uChar.UnicodeChar >= 0x20) {
            const wchar_t character[2] = {record.uChar.UnicodeChar, L'\0'};
            char utf8[4] = {};
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, character, 1, utf8,
                                                  static_cast<int>(sizeof(utf8)), nullptr, nullptr);
            m_pending.assign(utf8, utf8 + bytes);
            const auto first = static_cast<unsigned char>(m_pending.front());
            m_pending.erase(0, 1);
            return character(first);
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
    static Key character(unsigned char byte) {
        return Key{Key::Kind::Character, static_cast<char>(byte)};
    }

    HANDLE m_input{INVALID_HANDLE_VALUE};
    HANDLE m_output{INVALID_HANDLE_VALUE};
    DWORD m_savedInputMode{0};
    DWORD m_savedOutputMode{0};
    /// UTF-8 bytes of a wide character not yet handed over, one byte per readKey.
    std::string m_pending;
};

}  // namespace

std::unique_ptr<Terminal> makeTerminal() {
    return std::make_unique<WindowsTerminal>();
}

}  // namespace OpenAnimaTui

#endif  // defined(_WIN32)
