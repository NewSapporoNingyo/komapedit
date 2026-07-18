/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "json.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kme::json {
namespace {

class Parser {
public:
    explicit Parser(std::string_view source) : source_(source) {}

    Value parse_document() {
        skip_whitespace();
        Value value = parse_value();
        skip_whitespace();
        if (!at_end()) fail("unexpected trailing data");
        return value;
    }

private:
    std::string_view source_;
    size_t position_ = 0;

    bool at_end() const noexcept { return position_ >= source_.size(); }
    char peek() const noexcept { return at_end() ? '\0' : source_[position_]; }

    char take() {
        if (at_end()) fail("unexpected end of input");
        return source_[position_++];
    }

    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(
            "invalid JSON at byte " + std::to_string(position_) + ": " + message);
    }

    void expect(char expected, const char* message) {
        if (take() != expected) fail(message);
    }

    void skip_whitespace() noexcept {
        while (!at_end()) {
            const char ch = peek();
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            ++position_;
        }
    }

    static void append_utf8(std::string& out, unsigned codepoint) {
        if (codepoint <= 0x7f) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    unsigned parse_hex4() {
        unsigned value = 0;
        for (int digit = 0; digit < 4; ++digit) {
            const char ch = take();
            value <<= 4;
            if (ch >= '0' && ch <= '9') value |= static_cast<unsigned>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') value |= static_cast<unsigned>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') value |= static_cast<unsigned>(ch - 'A' + 10);
            else fail("invalid unicode escape");
        }
        return value;
    }

    unsigned parse_unicode_escape() {
        const unsigned first = parse_hex4();
        if (first >= 0xd800 && first <= 0xdbff) {
            if (take() != '\\' || take() != 'u') fail("missing low surrogate");
            const unsigned second = parse_hex4();
            if (second < 0xdc00 || second > 0xdfff) fail("invalid low surrogate");
            return 0x10000u + ((first - 0xd800u) << 10) + (second - 0xdc00u);
        }
        if (first >= 0xdc00 && first <= 0xdfff) fail("unexpected low surrogate");
        return first;
    }

    std::string parse_string_text() {
        expect('"', "expected string");
        std::string out;
        while (!at_end()) {
            const unsigned char ch = static_cast<unsigned char>(take());
            if (ch == '"') return out;
            if (ch < 0x20) fail("unescaped control character in string");
            if (ch != '\\') {
                out.push_back(static_cast<char>(ch));
                continue;
            }

            const char escaped = take();
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': append_utf8(out, parse_unicode_escape()); break;
                default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }

    Value parse_string() {
        Value value;
        value.type = Value::Type::String;
        value.string = parse_string_text();
        return value;
    }

    Value parse_number() {
        const size_t begin = position_;
        if (peek() == '-') ++position_;
        if (at_end()) fail("incomplete number");

        if (peek() == '0') {
            ++position_;
            if (!at_end() && peek() >= '0' && peek() <= '9') fail("leading zero in number");
        } else if (peek() >= '1' && peek() <= '9') {
            do {
                ++position_;
            } while (!at_end() && peek() >= '0' && peek() <= '9');
        } else {
            fail("invalid number integer part");
        }

        if (!at_end() && peek() == '.') {
            ++position_;
            if (at_end() || peek() < '0' || peek() > '9') fail("missing fraction digits");
            do {
                ++position_;
            } while (!at_end() && peek() >= '0' && peek() <= '9');
        }

        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            ++position_;
            if (!at_end() && (peek() == '+' || peek() == '-')) ++position_;
            if (at_end() || peek() < '0' || peek() > '9') fail("missing exponent digits");
            do {
                ++position_;
            } while (!at_end() && peek() >= '0' && peek() <= '9');
        }

        const std::string_view token = source_.substr(begin, position_ - begin);
        std::array<char, 128> local{};
        std::string large;
        const char* number_text = nullptr;
        if (token.size() < local.size()) {
            std::copy(token.begin(), token.end(), local.begin());
            number_text = local.data();
        } else {
            large.assign(token);
            number_text = large.c_str();
        }

        errno = 0;
        char* end = nullptr;
        const double parsed = std::strtod(number_text, &end);
        if (!end || static_cast<size_t>(end - number_text) != token.size()) {
            fail("invalid number conversion");
        }

        Value value;
        value.type = Value::Type::Number;
        value.number = parsed;
        return value;
    }

    Value parse_array() {
        Value value;
        value.type = Value::Type::Array;
        expect('[', "expected array");
        skip_whitespace();
        if (peek() == ']') {
            ++position_;
            return value;
        }
        while (true) {
            value.array.push_back(parse_value());
            skip_whitespace();
            const char separator = take();
            if (separator == ']') return value;
            if (separator != ',') fail("expected array separator");
            skip_whitespace();
        }
    }

    Value parse_object() {
        Value value;
        value.type = Value::Type::Object;
        expect('{', "expected object");
        skip_whitespace();
        if (peek() == '}') {
            ++position_;
            return value;
        }
        while (true) {
            if (peek() != '"') fail("expected object key");
            std::string key = parse_string_text();
            skip_whitespace();
            expect(':', "expected object colon");
            skip_whitespace();
            value.object[std::move(key)] = parse_value();
            skip_whitespace();
            const char separator = take();
            if (separator == '}') return value;
            if (separator != ',') fail("expected object separator");
            skip_whitespace();
        }
    }

    void parse_literal(std::string_view literal) {
        if (source_.substr(position_, literal.size()) != literal) fail("invalid literal");
        position_ += literal.size();
    }

    Value parse_value() {
        skip_whitespace();
        const char ch = peek();
        if (ch == '"') return parse_string();
        if (ch == '[') return parse_array();
        if (ch == '{') return parse_object();
        if (ch == '-' || (ch >= '0' && ch <= '9')) return parse_number();
        if (ch == 't') {
            parse_literal("true");
            Value value;
            value.type = Value::Type::Bool;
            value.boolean = true;
            return value;
        }
        if (ch == 'f') {
            parse_literal("false");
            Value value;
            value.type = Value::Type::Bool;
            return value;
        }
        if (ch == 'n') {
            parse_literal("null");
            return {};
        }
        fail("unexpected value");
    }
};

std::string finite_number_text(double value) {
    std::array<char, 64> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
    if (written > 0 && static_cast<size_t>(written) < buffer.size()) {
        return std::string(buffer.data(), static_cast<size_t>(written));
    }
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

} // namespace

const Value& Value::at(std::string_view key) const noexcept {
    static const Value empty;
    const auto found = object.find(key);
    return found == object.end() ? empty : found->second;
}

std::string Value::scalar_text() const {
    if (is_string()) return string;
    if (is_bool()) return boolean ? "true" : "false";
    if (!is_number()) return {};
    if (std::isnan(number)) return "null";
    if (std::isinf(number)) return number > 0.0 ? "1e999" : "-1e999";
    return finite_number_text(number);
}

std::string Value::scalar_text_fixed(int precision) const {
    if (!is_number()) return scalar_text();
    return number_text_fixed(number, precision);
}

std::string number_text_fixed(double value, int precision) {
    if (!std::isfinite(value)) return {};
    precision = std::max(0, precision);
    std::array<char, 128> stack_buffer{};
    int written = std::snprintf(stack_buffer.data(), stack_buffer.size(), "%.*f",
                                precision, value);
    if (written < 0) return {};
    std::string text;
    if (static_cast<size_t>(written) < stack_buffer.size()) {
        text.assign(stack_buffer.data(), static_cast<size_t>(written));
    } else {
        std::vector<char> buffer(static_cast<size_t>(written) + 1u);
        written = std::snprintf(buffer.data(), buffer.size(), "%.*f", precision, value);
        if (written < 0) return {};
        text.assign(buffer.data(), static_cast<size_t>(written));
    }
    const size_t point = text.find('.');
    if (point != std::string::npos) {
        while (text.size() > point + 1 && text.back() == '0') text.pop_back();
        if (text.size() == point + 1) text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

Value parse(std::string_view source) {
    return Parser(source).parse_document();
}

void append_escaped(std::ostream& out, std::string_view text) {
    for (const unsigned char ch : text) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(ch) << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }
}

void append_string(std::ostream& out, std::string_view text) {
    out << '"';
    append_escaped(out, text);
    out << '"';
}

std::string escape(std::string_view text) {
    std::ostringstream out;
    append_escaped(out, text);
    return out.str();
}

std::string quote(std::string_view text) {
    std::ostringstream out;
    append_string(out, text);
    return out.str();
}

} // namespace kme::json
