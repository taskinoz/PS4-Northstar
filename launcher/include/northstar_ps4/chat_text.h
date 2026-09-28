#pragma once

// Chat text handling ported from PC Northstar, free of game addresses so it
// can be host-tested (tests/chat_text.cpp):
//   - RemoveAsciiControlSequences: primedev/util/utils.cpp, applied to chat
//     the server receives and the client displays;
//   - WriteChatText: LocalChatWriter::Write and its ANSI escape parser
//     (primedev/client/localchatwriter.cpp), which NSChatWrite uses to turn
//     "\x1b[..m" sequences into colour changes;
//   - Utf8ToUtf16: the conversion before text reaches a rich-text panel, which
//     on PS4 takes UTF-16.

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace northstar::ps4::chat {

inline bool SkipValidAnsiCsiSgr(char*& str) {
    if (*str++ != '\x1B') return false;
    if (*str++ != '[') return false;  // CSI
    for (char* c = str; *c; c++) {
        if (*c >= '0' && *c <= '9') continue;
        if (*c == ';' || *c == ':') continue;
        if (*c == 'm') break;  // SGR
        return false;
    }
    return true;
}

// Replaces control characters and broken UTF-8 with spaces, keeping newlines
// and, when allowed, well-formed colour sequences.
inline void RemoveAsciiControlSequences(char* str, bool allowColorCodes) {
    for (char *pc = str, c = *pc; (c = *pc) != 0; pc++) {
        int bytesToSkip = 0;
        if ((c & 0xE0) == 0xC0)
            bytesToSkip = 1;
        else if ((c & 0xF0) == 0xE0)
            bytesToSkip = 2;
        else if ((c & 0xF8) == 0xF0)
            bytesToSkip = 3;
        else if ((c & 0xFC) == 0xF8)
            bytesToSkip = 4;
        else if ((c & 0xFE) == 0xFC)
            bytesToSkip = 5;

        bool invalid = false;
        char* orgpc = pc;
        for (int i = 0; i < bytesToSkip; i++) {
            char next = pc[1];
            if ((next & 0xC0) == 0x80) {
                pc++;
                continue;
            }
            invalid = true;
            break;
        }
        if (invalid) {
            for (char* x = orgpc; x <= pc; x++)
                if (*x != '\0')
                    *x = ' ';
                else
                    break;
        }
        if (bytesToSkip > 0) continue;

        if ((std::iscntrl(static_cast<unsigned char>(c)) && c != '\n' && c != '\r' && c != '\x1B') || (c & 0x80) != 0) {
            *pc = ' ';
            continue;
        }

        if (c == '\x1B') {
            if (allowColorCodes && SkipValidAnsiCsiSgr(pc))
                pc--;
            else
                *pc = ' ';
        }
    }
}

struct ChatColor {
    std::uint8_t r, g, b, a;
    std::uint32_t Packed() const {
        return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) |
            (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(a) << 24);
    }
};

enum class ChatSwatch { MainText, SameTeamName, EnemyTeamName, NetworkName };

inline constexpr ChatColor kDarkColors[8] = {{0, 0, 0, 255}, {205, 49, 49, 255}, {13, 188, 121, 255},
    {229, 229, 16, 255}, {36, 114, 200, 255}, {188, 63, 188, 255}, {17, 168, 205, 255}, {229, 229, 229, 255}};
inline constexpr ChatColor kLightColors[8] = {{102, 102, 102, 255}, {241, 76, 76, 255}, {35, 209, 139, 255},
    {245, 245, 67, 255}, {59, 142, 234, 255}, {214, 112, 214, 255}, {41, 184, 219, 255}, {255, 255, 255, 255}};

// Writer needs Text(const char* text, std::size_t length),
// Color(ChatColor) and Swatch(ChatSwatch).
template <typename Writer>
class AnsiEscapeParser {
public:
    explicit AnsiEscapeParser(Writer& writer) : m_writer(writer) {}

    void HandleVal(unsigned long val) {
        switch (m_next) {
        case Next::ControlType: m_next = HandleControlType(val); break;
        case Next::ForegroundType: m_next = HandleForegroundType(val); break;
        case Next::Foreground8Bit: m_next = HandleForeground8Bit(val); break;
        case Next::ForegroundR: m_next = HandleForegroundChannel(val, 0, Next::ForegroundG); break;
        case Next::ForegroundG: m_next = HandleForegroundChannel(val, 1, Next::ForegroundB); break;
        case Next::ForegroundB:
            m_next = HandleForegroundChannel(val, 2, Next::ControlType);
            if (val < 255) m_writer.Color(m_expanded);
            break;
        }
    }

private:
    enum class Next { ControlType, ForegroundType, Foreground8Bit, ForegroundR, ForegroundG, ForegroundB };

    Writer& m_writer;
    Next m_next = Next::ControlType;
    ChatColor m_expanded{0, 0, 0, 0};

    Next HandleControlType(unsigned long val) {
        if (val == 0 || val == 39) {
            m_writer.Swatch(ChatSwatch::MainText);
            return Next::ControlType;
        }
        if (val >= 30 && val < 38) {
            m_writer.Color(kDarkColors[val - 30]);
            return Next::ControlType;
        }
        if (val >= 90 && val < 98) {
            m_writer.Color(kLightColors[val - 90]);
            return Next::ControlType;
        }
        if (val >= 110 && val < 114) {
            m_writer.Swatch(static_cast<ChatSwatch>(val - 110));
            return Next::ControlType;
        }
        if (val == 38) return Next::ForegroundType;
        return Next::ControlType;
    }

    Next HandleForegroundType(unsigned long val) {
        if (val == 2) {
            m_expanded = {0, 0, 0, 255};
            return Next::ForegroundR;
        }
        if (val == 5) return Next::Foreground8Bit;
        return Next::ControlType;
    }

    Next HandleForeground8Bit(unsigned long val) {
        if (val < 8) {
            m_writer.Color(kDarkColors[val]);
        } else if (val < 16) {
            m_writer.Color(kLightColors[val - 8]);
        } else if (val < 232) {
            const unsigned char code = static_cast<unsigned char>(val - 16);
            const unsigned char blue = code % 6;
            const unsigned char green = ((code - blue) / 6) % 6;
            const unsigned char red = (code - blue - (green * 6)) / 36;
            m_writer.Color({static_cast<std::uint8_t>(red * 51), static_cast<std::uint8_t>(green * 51),
                static_cast<std::uint8_t>(blue * 51), 255});
        } else if (val < 255) {
            const auto brightness = static_cast<std::uint8_t>((val - 232) * 10 + 8);
            m_writer.Color({brightness, brightness, brightness, 255});
        }
        return Next::ControlType;
    }

    Next HandleForegroundChannel(unsigned long val, int channel, Next next) {
        if (val >= 255) return Next::ControlType;
        std::uint8_t* channels[] = {&m_expanded.r, &m_expanded.g, &m_expanded.b};
        *channels[channel] = static_cast<std::uint8_t>(val);
        return next;
    }
};

// LocalChatWriter::ApplyAnsiEscape: `escape` is just past "\x1b["; returns
// where text resumes.
template <typename Writer>
const char* ApplyAnsiEscape(const char* escape, Writer& writer) {
    AnsiEscapeParser<Writer> decoder(writer);
    while (true) {
        char* afterControlType = nullptr;
        const unsigned long controlType = std::strtoul(escape, &afterControlType, 10);
        if (afterControlType == nullptr || (controlType == 0 && escape[0] != '0')) return escape;
        decoder.HandleVal(controlType);
        if (afterControlType[0] == 'm') return afterControlType + 1;
        if (afterControlType[0] != ':' && afterControlType[0] != ';') return afterControlType;
        escape = afterControlType + 1;
    }
}

// LocalChatWriter::Write. PC copies the text before an escape through a
// 256-byte buffer, which cuts it at 255 bytes; that is kept.
template <typename Writer>
void WriteChatText(const char* str, Writer& writer) {
    while (true) {
        const char* startOfEscape = std::strstr(str, "\033[");
        if (!startOfEscape) {
            writer.Text(str, std::strlen(str));
            return;
        }
        if (startOfEscape != str) {
            std::size_t copyChars = static_cast<std::size_t>(startOfEscape - str);
            if (copyChars > 255) copyChars = 255;
            writer.Text(str, copyChars);
        }
        str = ApplyAnsiEscape(startOfEscape + 2, writer);
    }
}

// UTF-8 to UTF-16; malformed bytes become U+FFFD.
inline std::u16string Utf8ToUtf16(const char* text, std::size_t length) {
    std::u16string out;
    out.reserve(length);
    std::size_t i = 0;
    while (i < length) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F; extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F; extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07; extra = 3;
        } else {
            out += u'�';
            ++i;
            continue;
        }
        bool ok = i + static_cast<std::size_t>(extra) < length;
        for (int k = 1; ok && k <= extra; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (next & 0x3F);
        }
        if (!ok || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out += u'�';
            ++i;
            continue;
        }
        i += static_cast<std::size_t>(extra) + 1;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (cp >> 10));
            out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out += static_cast<char16_t>(cp);
        }
    }
    return out;
}

} // namespace northstar::ps4::chat
