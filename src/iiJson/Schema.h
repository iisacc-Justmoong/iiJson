#pragma once
#include <iiJson/Json.h>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace ii::json {
namespace detail { struct SchemaNode; class SchemaValidator; class SchemaDefinitionCodec; }

enum class SchemaKind { any, null, boolean, integer, unsigned_integer, number,
                        string, array, object, enumeration, one_of };
struct SchemaBudget final {
    std::size_t remaining_steps = 100000;
};

enum class Presence { required, optional };
enum class AdditionalFields { reject, allow };
struct SizeBounds final {
    std::size_t minimum = 0;
    std::size_t maximum = std::numeric_limits<std::size_t>::max();
};
struct Field;

// Immutable, owned schema definitions. Copies share only const state; composing
// a nullable/refined schema never changes the definition it was derived from.
class Schema final {
public:
    using Validator = std::function<Result<void>(const Value&)>;
    Schema(); // Any JSON value, still subject to the JSON resource limits.
    [[nodiscard]] static Schema any();
    [[nodiscard]] static Schema null();
    [[nodiscard]] static Schema boolean();
    [[nodiscard]] static Schema integer(
        std::int64_t minimum = std::numeric_limits<std::int64_t>::min(),
        std::int64_t maximum = std::numeric_limits<std::int64_t>::max());
    [[nodiscard]] static Schema unsigned_integer(
        std::uint64_t minimum = 0,
        std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max());
    [[nodiscard]] static Schema number(std::optional<double> minimum = {},
                                       std::optional<double> maximum = {});
    // Strings count Unicode scalar values; arrays count elements. Bounds include
    // both endpoints. The independent Limits::string_bytes still bounds bytes.
    [[nodiscard]] static Schema string(SizeBounds bounds = {});
    [[nodiscard]] static Schema array(Schema element, SizeBounds bounds = {});
    [[nodiscard]] static Schema object(std::vector<Field> fields,
                                        AdditionalFields additional = AdditionalFields::reject);
    [[nodiscard]] static Schema literal(Value value);
    [[nodiscard]] static Schema enumeration(std::vector<Value> values);
    // Exactly one alternative must match; overlapping alternatives are rejected.
    [[nodiscard]] static Schema one_of(std::vector<Schema> alternatives);
    [[nodiscard]] Schema nullable() const;
    // Runs after structural validation. Errors use a pointer relative to this
    // value. The callable is owned; captured references remain the caller's
    // responsibility. Exceptions from the callable propagate.
    [[nodiscard]] Schema refine(Validator validator) const;
    [[nodiscard]] Result<void> validate(const Value& value, Limits limits = {}) const;
    [[nodiscard]] Result<void> validate(const Value& value, Limits limits, SchemaBudget& budget) const;
    [[nodiscard]] SchemaKind kind() const noexcept;
    [[nodiscard]] bool nullable_enabled() const noexcept;
    // Versioned, portable definition. Native refinement callbacks cannot be
    // serialized and cause unsupported_value rather than being discarded.
    [[nodiscard]] Result<Value> definition(Limits limits = {}) const;
    [[nodiscard]] static Result<Schema> from_definition(const Value& definition, Limits limits = {});

private:
    explicit Schema(std::shared_ptr<const detail::SchemaNode> node);
    std::shared_ptr<const detail::SchemaNode> node_;
    friend class detail::SchemaValidator;
    friend class detail::SchemaDefinitionCodec;
};

template<> struct Codec<Schema> final {
    [[nodiscard]] static Result<Value> encode(const Schema& schema) { return schema.definition(); }
    [[nodiscard]] static Result<Schema> decode(const Value& value) { return Schema::from_definition(value); }
};

struct Field final {
    std::string name;
    Schema schema;
    Presence presence = Presence::required;
    [[nodiscard]] static Field required(std::string name, Schema schema);
    [[nodiscard]] static Field optional(std::string name, Schema schema);
};

namespace detail {
[[nodiscard]] Result<Value> parseSchema(std::string_view input, const Schema& schema, Limits limits);
[[nodiscard]] Result<std::string> stringifySchema(const Value& value, const Schema& schema, Limits limits);
}
// Deduction deliberately excludes {} so existing parse(text, {}) and
// stringify(value, {}) continue to select the original Limits overloads.
template<class S> requires std::same_as<std::remove_cvref_t<S>, Schema>
[[nodiscard]] Result<Value> parse(std::string_view input, S&& schema, Limits limits = {}) {
    return detail::parseSchema(input, schema, limits);
}
template<class S> requires std::same_as<std::remove_cvref_t<S>, Schema>
[[nodiscard]] Result<std::string> stringify(const Value& value, S&& schema, Limits limits = {}) {
    return detail::stringifySchema(value, schema, limits);
}
} // namespace ii::json
