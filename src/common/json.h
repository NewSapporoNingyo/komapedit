/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace kme::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::map<std::string, Value, std::less<>> object;

    bool is_null() const noexcept { return type == Type::Null; }
    bool is_bool() const noexcept { return type == Type::Bool; }
    bool is_number() const noexcept { return type == Type::Number; }
    bool is_string() const noexcept { return type == Type::String; }
    bool is_array() const noexcept { return type == Type::Array; }
    bool is_object() const noexcept { return type == Type::Object; }

    const Value& at(std::string_view key) const noexcept;
    std::string scalar_text() const;
    std::string scalar_text_fixed(int precision) const;
};

Value parse(std::string_view source);

/* Shared fixed-precision scalar formatter used by JSON and typed preview
   hydration. It intentionally matches Value::scalar_text_fixed(). */
std::string number_text_fixed(double value, int precision);

void append_escaped(std::ostream& out, std::string_view text);
void append_string(std::ostream& out, std::string_view text);
std::string escape(std::string_view text);
std::string quote(std::string_view text);

} // namespace kme::json
