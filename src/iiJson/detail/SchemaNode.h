#pragma once
#include <iiJson/Schema.h>
#include <map>
#include <variant>

namespace ii::json::detail {
inline constexpr std::size_t maximumSchemaDepth = 256;
struct Any {};
struct Null {};
struct Boolean {};
struct Integer { std::int64_t minimum, maximum; };
struct UnsignedInteger { std::uint64_t minimum, maximum; };
struct Number { std::optional<double> minimum, maximum; };
struct String { SizeBounds bounds; };
struct Array { Schema element; SizeBounds bounds; };
struct Object { std::map<std::string, Field, std::less<>> fields; AdditionalFields additional; };
struct Enumeration { std::vector<Value> values; };
struct OneOf { std::vector<Schema> alternatives; };
struct SchemaNode final {
    using Rule = std::variant<Any, Null, Boolean, Integer, UnsignedInteger, Number,
                              String, Array, Object, Enumeration, OneOf>;
    explicit SchemaNode(Rule definition) : rule(std::move(definition)) {}
    Rule rule;
    bool nullable = false;
    std::vector<Schema::Validator> validators;
};
} // namespace ii::json::detail
