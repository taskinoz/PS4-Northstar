#pragma once

// A small backtracking regular expression matcher for mod-supplied patterns
// (audio EventIdRegex). The PS4 build has no C++ exceptions, and libc++'s
// std::regex aborts on a malformed pattern there, so one bad mod file would
// take the game down. This covers the ECMAScript subset such patterns use:
//   literals and escapes (\. \\ \d \D \w \W \s \S), ".", classes [a-z_] and
//   [^...], groups (...) and (?:...), alternation "|", the quantifiers * + ?
//   {n} {n,} {n,m} (a trailing "?" for lazy is accepted), and ^ $.
// Compile returns false with a message for anything else. Search matches
// anywhere in the text, as std::regex_search does. Host-tested in
// tests/regex_lite.cpp.

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace northstar::ps4::regexlite {

class Regex {
public:
    bool Compile(const std::string& pattern, std::string& error) {
        pattern_ = pattern;
        pos_ = 0;
        error_.clear();
        root_ = std::make_shared<Node>();
        root_->kind = Kind::Group;
        if (!ParseAlternatives(root_->alternatives) || pos_ != pattern_.size()) {
            if (error_.empty()) error_ = "unexpected ')' at " + std::to_string(pos_);
            error = error_;
            root_.reset();
            return false;
        }
        return true;
    }

    bool Search(const char* text) const {
        if (!root_) return false;
        text_ = text;
        length_ = std::strlen(text);
        steps_ = 0;
        for (std::size_t start = 0; start <= length_; ++start)
            if (MatchOne(*root_, start, [](std::size_t) { return true; })) return true;
        return false;
    }

private:
    enum class Kind { Char, Any, Class, Group, Start, End };
    struct Node {
        Kind kind = Kind::Char;
        char c = 0;
        bool negate = false;
        std::vector<std::pair<unsigned char, unsigned char>> ranges;
        std::vector<std::vector<Node>> alternatives;
        int min = 1;
        int max = 1; // -1: unbounded
    };

    // Parsing ------------------------------------------------------------
    bool Fail(const std::string& message) {
        if (error_.empty()) error_ = message + " at " + std::to_string(pos_);
        return false;
    }
    bool More() const { return pos_ < pattern_.size(); }
    char Peek() const { return pattern_[pos_]; }

    bool ParseAlternatives(std::vector<std::vector<Node>>& out) {
        out.emplace_back();
        while (More() && Peek() != ')') {
            if (Peek() == '|') {
                ++pos_;
                out.emplace_back();
                continue;
            }
            Node node;
            if (!ParseAtom(node) || !ParseQuantifier(node)) return false;
            out.back().push_back(std::move(node));
        }
        return true;
    }

    static void AddShorthand(Node& node, char c) {
        auto add = [&](unsigned char a, unsigned char b) { node.ranges.emplace_back(a, b); };
        switch (c) {
            case 'd': case 'D': add('0', '9'); break;
            case 'w': case 'W': add('a', 'z'); add('A', 'Z'); add('0', '9'); add('_', '_'); break;
            case 's': case 'S': add(' ', ' '); add('\t', '\r'); break;
        }
    }

    bool ParseEscape(Node& node) {
        if (!More()) return Fail("trailing backslash");
        const char c = pattern_[pos_++];
        if (std::strchr("dDwWsS", c)) {
            node.kind = Kind::Class;
            node.negate = c >= 'A' && c <= 'Z';
            AddShorthand(node, static_cast<char>(c | 0x20));
            return true;
        }
        if (c == 't') { node.kind = Kind::Char; node.c = '\t'; return true; }
        if (c == 'n') { node.kind = Kind::Char; node.c = '\n'; return true; }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            return Fail(std::string("unsupported escape \\") + c);
        node.kind = Kind::Char;
        node.c = c;
        return true;
    }

    bool ParseClass(Node& node) {
        node.kind = Kind::Class;
        if (More() && Peek() == '^') {
            node.negate = true;
            ++pos_;
        }
        bool first = true;
        while (More() && (Peek() != ']' || first)) {
            first = false;
            unsigned char low = static_cast<unsigned char>(pattern_[pos_++]);
            if (low == '\\') {
                if (!More()) return Fail("trailing backslash");
                const char c = pattern_[pos_++];
                if (std::strchr("dwsDWS", c)) {
                    if (c >= 'A' && c <= 'Z') return Fail("negated shorthand in a class");
                    AddShorthand(node, c);
                    continue;
                }
                low = static_cast<unsigned char>(c == 't' ? '\t' : c == 'n' ? '\n' : c);
            }
            unsigned char high = low;
            if (pos_ + 1 < pattern_.size() && Peek() == '-' && pattern_[pos_ + 1] != ']') {
                ++pos_;
                high = static_cast<unsigned char>(pattern_[pos_++]);
                if (high == '\\') {
                    if (!More()) return Fail("trailing backslash");
                    high = static_cast<unsigned char>(pattern_[pos_++]);
                }
                if (high < low) return Fail("range out of order");
            }
            node.ranges.emplace_back(low, high);
        }
        if (!More()) return Fail("missing ']'");
        ++pos_;
        return true;
    }

    bool ParseAtom(Node& node) {
        const char c = pattern_[pos_++];
        switch (c) {
            case '.': node.kind = Kind::Any; return true;
            case '^': node.kind = Kind::Start; return true;
            case '$': node.kind = Kind::End; return true;
            case '\\': return ParseEscape(node);
            case '[': return ParseClass(node);
            case '(': {
                if (pos_ + 1 < pattern_.size() && Peek() == '?') {
                    if (pattern_[pos_ + 1] != ':') return Fail("unsupported group");
                    pos_ += 2;
                }
                node.kind = Kind::Group;
                if (!ParseAlternatives(node.alternatives)) return false;
                if (!More() || Peek() != ')') return Fail("missing ')'");
                ++pos_;
                return true;
            }
            case '*': case '+': case '?': case '{': return Fail("nothing to repeat");
            case ')': return Fail("unexpected ')'");
            default: node.kind = Kind::Char; node.c = c; return true;
        }
    }

    bool ReadNumber(int& out) {
        if (!More() || Peek() < '0' || Peek() > '9') return false;
        out = 0;
        while (More() && Peek() >= '0' && Peek() <= '9') {
            out = out * 10 + (Peek() - '0');
            if (out > 1000) return false;
            ++pos_;
        }
        return true;
    }

    bool ParseQuantifier(Node& node) {
        if (!More()) return true;
        const char c = Peek();
        if (c == '*') { node.min = 0; node.max = -1; }
        else if (c == '+') { node.min = 1; node.max = -1; }
        else if (c == '?') { node.min = 0; node.max = 1; }
        else if (c == '{') {
            const std::size_t start = pos_++;
            int low = 0, high = 0;
            if (!ReadNumber(low)) { pos_ = start; return Fail("bad repeat count"); }
            high = low;
            if (More() && Peek() == ',') {
                ++pos_;
                if (More() && Peek() == '}') high = -1;
                else if (!ReadNumber(high) || high < low) return Fail("bad repeat count");
            }
            if (!More() || Peek() != '}') return Fail("missing '}'");
            node.min = low;
            node.max = high;
        } else {
            return true;
        }
        if (node.kind == Kind::Start || node.kind == Kind::End) return Fail("nothing to repeat");
        ++pos_;
        if (More() && Peek() == '?') ++pos_; // lazy: same answer for a search
        if (More() && (Peek() == '*' || Peek() == '+' || Peek() == '{')) return Fail("nothing to repeat");
        return true;
    }

    // Matching -------------------------------------------------------------
    using Next = std::function<bool(std::size_t)>;

    bool MatchSequence(const std::vector<Node>& sequence, std::size_t index, std::size_t at, const Next& next) const {
        if (index == sequence.size()) return next(at);
        return MatchRepeat(sequence[index], 0, at,
            [&](std::size_t p) { return MatchSequence(sequence, index + 1, p, next); });
    }

    bool MatchRepeat(const Node& node, int count, std::size_t at, const Next& next) const {
        if (++steps_ > kMaxSteps) return false;
        if (node.max < 0 || count < node.max) {
            const bool matched = MatchOne(node, at, [&](std::size_t p) {
                if (p == at && count >= node.min) return false; // an empty match can't loop
                return MatchRepeat(node, count + 1, p, next);
            });
            if (matched) return true;
        }
        return count >= node.min && next(at);
    }

    bool MatchOne(const Node& node, std::size_t at, const Next& next) const {
        switch (node.kind) {
            case Kind::Char: return at < length_ && text_[at] == node.c && next(at + 1);
            case Kind::Any: return at < length_ && text_[at] != '\n' && next(at + 1);
            case Kind::Start: return at == 0 && next(at);
            case Kind::End: return at == length_ && next(at);
            case Kind::Class: {
                if (at >= length_) return false;
                const auto c = static_cast<unsigned char>(text_[at]);
                bool in = false;
                for (const auto& range : node.ranges) in = in || (c >= range.first && c <= range.second);
                return in != node.negate && next(at + 1);
            }
            case Kind::Group:
                for (const auto& alternative : node.alternatives)
                    if (MatchSequence(alternative, 0, at, next)) return true;
                return false;
        }
        return false;
    }

    static constexpr unsigned kMaxSteps = 200000; // a pathological pattern gives up, unmatched

    std::string pattern_;
    std::size_t pos_ = 0;
    std::string error_;
    std::shared_ptr<Node> root_;
    mutable const char* text_ = nullptr;
    mutable std::size_t length_ = 0;
    mutable unsigned steps_ = 0;
};

} // namespace northstar::ps4::regexlite
