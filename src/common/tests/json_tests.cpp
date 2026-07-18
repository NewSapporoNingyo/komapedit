/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "json.h"

#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void expect_rejected(std::string_view source, const char* message) {
    try {
        (void)kme::json::parse(source);
        check(false, message);
    } catch (const std::runtime_error& error) {
        check(std::string(error.what()).find("byte ") != std::string::npos,
              "parse errors include a byte offset");
    }
}

std::string legacy_fixed(double value, int precision) {
    if (!std::isfinite(value)) return {};
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    std::string text = out.str();
    const size_t point = text.find('.');
    if (point != std::string::npos) {
        while (text.size() > point + 1 && text.back() == '0') text.pop_back();
        if (text.size() == point + 1) text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

} // namespace

int main() {
    using kme::json::Value;

    const Value root = kme::json::parse(
        R"({"null":null,"bool":true,"number":-12.5e2,"string":"text","array":[1,false],"object":{"key":"value"}})");
    check(root.is_object(), "root object");
    check(root.at("null").is_null(), "null value");
    check(root.at("bool").is_bool() && root.at("bool").boolean, "boolean value");
    check(root.at("number").is_number() && root.at("number").number == -1250.0,
          "number value");
    check(root.at("string").string == "text", "string value");
    check(root.at("array").is_array() && root.at("array").array.size() == 2,
          "array value");
    check(root.at("object").at("key").string == "value", "nested object");
    check(root.at("missing").is_null(), "missing keys return null");

    const Value escaped = kme::json::parse(
        R"("\"\\\/\b\f\n\r\t\u0041\u3042\uD83D\uDE80")");
    check(escaped.string == std::string("\"\\/\b\f\n\r\tA") +
              "\xE3\x81\x82\xF0\x9F\x9A\x80",
          "escape and surrogate decoding");

    const Value duplicate = kme::json::parse(R"({"key":1,"key":2})");
    check(duplicate.at("key").number == 2.0, "duplicate keys use the last value");

    const Value overflow = kme::json::parse("1e999");
    check(overflow.is_number() && std::isinf(overflow.number) && overflow.number > 0.0,
          "overflowing valid number is retained as infinity");
    check(overflow.scalar_text() == "1e999", "infinity scalar compatibility");

    const std::vector<double> fixed_boundaries = {
        0.0, -0.0, 0.0000004, 0.0000005, -0.0000005,
        1.2345674, 1.2345675, -1.2345675, 999999.9999995,
        std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
    };
    for (double value : fixed_boundaries) {
        check(kme::json::number_text_fixed(value, 6) == legacy_fixed(value, 6),
              "fast fixed formatter matches legacy formatter");
    }
    check(kme::json::number_text_fixed(std::numeric_limits<double>::infinity(), 6).empty(),
          "fixed formatter keeps non-finite compatibility");

    const std::string raw = std::string("quote=\" slash=\\ line=\n tab=\t control=") +
        static_cast<char>(1) + " utf8=\xE3\x81\x82";
    const std::string quoted = kme::json::quote(raw);
    check(kme::json::parse(quoted).string == raw, "quoted string round trip");

    const std::vector<std::string_view> invalid = {
        "", "true false", "01", "-", "1.", "1e", "[1,]", "{\"a\":1,}",
        "\"unterminated", "\"bad\\x\"", "\"bad\\u12xz\"", "\"\\uD800\"",
        "\"\\uDC00\"", "\"\\uD800\\u0041\"", "\"raw\x01control\"",
    };
    for (std::string_view source : invalid) expect_rejected(source, "invalid JSON rejected");

    if (failures != 0) {
        std::cerr << failures << " JSON test(s) failed\n";
        return 1;
    }
    std::cout << "JSON tests passed\n";
    return 0;
}
