#include <iiJson/Schema.h>
#include "detail/SchemaNode.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <variant>

namespace ii::json {
namespace detail {
namespace {
constexpr std::size_t maximumDepth = maximumSchemaDepth;

// Avoid double(integer) rounding at the int64/uint64 boundaries, including on
// platforms where long double has the same precision as double (MSVC).
template<class IntegerType> int compareInteger(IntegerType value, double bound) {
    if constexpr (std::is_signed_v<IntegerType>) {
        if (bound >= 0x1p63) return -1;
        if (bound < -0x1p63) return 1;
    } else {
        if (bound >= 0x1p64) return -1;
        if (bound < 0) return 1;
    }
    const auto whole = static_cast<IntegerType>(bound);
    if (value < whole) return -1;
    if (value > whole) return 1;
    const double fraction = bound - static_cast<double>(whole);
    return fraction > 0 ? -1 : fraction < 0 ? 1 : 0;
}
int compareNumber(const Value& value, double bound) {
    if (const auto* integer = std::get_if<std::int64_t>(&value.storage())) return compareInteger(*integer, bound);
    if (const auto* integer = std::get_if<std::uint64_t>(&value.storage())) return compareInteger(*integer, bound);
    const auto real = std::get<double>(value.storage());
    return real < bound ? -1 : real > bound ? 1 : 0;
}
bool inBounds(std::size_t count, SizeBounds bounds) {
    return count >= bounds.minimum && count <= bounds.maximum;
}
void checkBounds(SizeBounds bounds) {
    if (bounds.minimum > bounds.maximum) throw std::invalid_argument("Schema minimum exceeds maximum");
}
bool validPointer(std::string_view pointer) {
    if (pointer.empty()) return true;
    if (pointer.front() != '/' || !ii::json::detail::valid_utf8(pointer)) return false;
    for (std::size_t index = 0; index < pointer.size(); ++index) {
        if (pointer[index] != '~') continue;
        if (++index == pointer.size() || (pointer[index] != '0' && pointer[index] != '1')) return false;
    }
    return true;
}
// Restores the current pointer even when an alternative fails by throwing Error.
class PathScope final {
public:
    PathScope(std::string& path, std::string_view token) : path_(path), size_(path.size()) {
        path_ += '/';
        for (char byte : token) {
            if (byte == '~') path_ += "~0";
            else if (byte == '/') path_ += "~1";
            else path_ += byte;
        }
    }
    ~PathScope() { path_.resize(size_); }
    PathScope(const PathScope&) = delete;
    PathScope& operator=(const PathScope&) = delete;
private:
    std::string& path_;
    std::size_t size_;
};
}

class SchemaValidator final {
public:
    explicit SchemaValidator(Limits policy, SchemaBudget* budget = nullptr) : limits_(policy), budget_(budget) {}
    Result<void> run(const Schema& schema, const Value& value) {
        try {
            if (limits_.depth > maximumDepth) fail(ErrorCode::limit_exceeded, "JSON depth configuration exceeds hard ceiling");
            inspect(value, 1);
            match(schema, value, 1);
            return {};
        } catch (const Error& error) {
            return std::unexpected(error);
        }
    }
private:
    Limits limits_;
    SchemaBudget* budget_;
    std::size_t items_ = 0, steps_ = 0;
    std::string path_;
    [[noreturn]] void fail(ErrorCode code, const char* message) const {
        throw Error{code, 0, 1, 1, message, path_};
    }
    void require(bool condition, const char* message) const {
        if (!condition) fail(ErrorCode::schema_mismatch, message);
    }
    void count(std::size_t& counter, std::size_t maximum, const char* message) const {
        if (counter >= maximum) fail(ErrorCode::limit_exceeded, message);
        ++counter;
    }
    void step() {
        count(steps_, limits_.schema_steps, "Schema evaluation limit exceeded");
        if (budget_) {
            if (budget_->remaining_steps == 0) fail(ErrorCode::limit_exceeded, "Shared schema evaluation limit exceeded");
            --budget_->remaining_steps;
        }
    }
    void text(std::string_view value) const {
        if (value.size() > limits_.string_bytes) fail(ErrorCode::limit_exceeded, "JSON string byte limit exceeded");
    }
    // Inspect the entire tree, including any/unknown fields. These must not
    // bypass resource limits or admit Core's non-JSON binary alternative.
    void inspect(const Value& value, std::size_t depth) {
        count(items_, limits_.items, "JSON item limit exceeded");
        if (depth > limits_.depth || depth > maximumDepth) fail(ErrorCode::limit_exceeded, "JSON depth limit exceeded");
        std::visit([&](const auto& data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::same_as<T, ii::json::Bytes>) {
                fail(ErrorCode::unsupported_value, "Binary bytes need an explicit domain encoding for JSON");
            } else if constexpr (std::same_as<T, std::string>) {
                text(data);
            } else if constexpr (std::same_as<T, Value::Array>) {
                for (std::size_t index = 0; index < data.size(); ++index) {
                    const PathScope pointer(path_, std::to_string(index));
                    inspect(data[index], depth + 1);
                }
            } else if constexpr (std::same_as<T, Value::Object>) {
                for (const auto& [key, child] : data) {
                    const PathScope pointer(path_, key);
                    count(items_, limits_.items, "JSON item limit exceeded");
                    text(key);
                    inspect(child, depth + 1);
                }
            }
        }, value.storage());
    }
    bool equal(const Value& left, const Value& right) {
        step();
        if (left.kind() != right.kind()) {
            if (const auto* integer = std::get_if<std::int64_t>(&left.storage())) {
                const auto* other = std::get_if<std::uint64_t>(&right.storage());
                return other && *integer >= 0 && std::uint64_t(*integer) == *other;
            }
            if (const auto* integer = std::get_if<std::uint64_t>(&left.storage())) {
                const auto* other = std::get_if<std::int64_t>(&right.storage());
                return other && *other >= 0 && *integer == std::uint64_t(*other);
            }
            return false;
        }
        if (const auto* array = std::get_if<Value::Array>(&left.storage())) {
            const auto& other = std::get<Value::Array>(right.storage());
            if (array->size() != other.size()) return false;
            for (std::size_t index = 0; index < array->size(); ++index)
                if (!equal((*array)[index], other[index])) return false;
            return true;
        }
        if (const auto* object = std::get_if<Value::Object>(&left.storage())) {
            const auto& other = std::get<Value::Object>(right.storage());
            if (object->size() != other.size()) return false;
            auto entry = other.begin();
            for (const auto& [key, child] : *object) {
                if (key != entry->first || !equal(child, entry->second)) return false;
                ++entry;
            }
            return true;
        }
        return left == right;
    }
    void match(const Schema& schema, const Value& value, std::size_t depth) {
        step();
        if (depth > maximumDepth) fail(ErrorCode::limit_exceeded, "Schema depth limit exceeded");
        const auto& node = *schema.node_;
        if (node.nullable && value.kind() == ii::json::ValueKind::null) return;
        std::visit([&](const auto& rule) {
            using T = std::decay_t<decltype(rule)>;
            if constexpr (std::same_as<T, Any>) {
                return;
            } else if constexpr (std::same_as<T, Null>) {
                require(value.kind() == ii::json::ValueKind::null, "Expected null");
            } else if constexpr (std::same_as<T, Boolean>) {
                require(value.kind() == ii::json::ValueKind::boolean, "Expected boolean");
            } else if constexpr (std::same_as<T, Integer>) {
                if (const auto* integer = std::get_if<std::int64_t>(&value.storage())) {
                    require(*integer >= rule.minimum && *integer <= rule.maximum, "Signed integer is outside the domain range");
                } else if (const auto* integer = std::get_if<std::uint64_t>(&value.storage())) {
                    require(*integer <= std::uint64_t(INT64_MAX), "Expected an integer in signed 64-bit range");
                    const auto converted = std::int64_t(*integer);
                    require(converted >= rule.minimum && converted <= rule.maximum, "Signed integer is outside the domain range");
                } else require(false, "Expected an integer in signed 64-bit range");
            } else if constexpr (std::same_as<T, UnsignedInteger>) {
                std::uint64_t converted = 0;
                if (const auto* integer = std::get_if<std::uint64_t>(&value.storage())) converted = *integer;
                else if (const auto* integer = std::get_if<std::int64_t>(&value.storage())) {
                    require(*integer >= 0, "Expected a nonnegative integer");
                    converted = std::uint64_t(*integer);
                } else require(false, "Expected a nonnegative integer");
                require(converted >= rule.minimum && converted <= rule.maximum, "Unsigned integer is outside the domain range");
            } else if constexpr (std::same_as<T, Number>) {
                require(value.kind() == ii::json::ValueKind::signed_integer ||
                        value.kind() == ii::json::ValueKind::unsigned_integer ||
                        value.kind() == ii::json::ValueKind::real, "Expected a number");
                if (rule.minimum) require(compareNumber(value, *rule.minimum) >= 0, "Number is below the domain minimum");
                if (rule.maximum) require(compareNumber(value, *rule.maximum) <= 0, "Number exceeds the domain maximum");
            } else if constexpr (std::same_as<T, String>) {
                const auto* string = std::get_if<std::string>(&value.storage());
                require(string != nullptr, "Expected a string");
                const auto length = std::count_if(string->begin(), string->end(), [](unsigned char byte) {
                    return (byte & 0xc0) != 0x80;
                });
                require(inBounds(std::size_t(length), rule.bounds), "String length is outside the domain bounds");
            } else if constexpr (std::same_as<T, Array>) {
                const auto* array = std::get_if<Value::Array>(&value.storage());
                require(array != nullptr, "Expected an array");
                require(inBounds(array->size(), rule.bounds), "Array length is outside the domain bounds");
                for (std::size_t index = 0; index < array->size(); ++index) {
                    const PathScope pointer(path_, std::to_string(index));
                    match(rule.element, (*array)[index], depth + 1);
                }
            } else if constexpr (std::same_as<T, Object>) {
                const auto* object = std::get_if<Value::Object>(&value.storage());
                require(object != nullptr, "Expected an object");
                for (const auto& [name, field] : rule.fields) {
                    const PathScope pointer(path_, name);
                    step(); // Missing optional fields also consume schema work.
                    const auto found = object->find(name);
                    if (found == object->end()) require(field.presence == Presence::optional, "Required field is missing");
                    else match(field.schema, found->second, depth + 1);
                }
                if (rule.additional == AdditionalFields::reject) {
                    for (const auto& [name, child] : *object) {
                        (void)child;
                        const PathScope pointer(path_, name);
                        step();
                        require(rule.fields.contains(name), "Unknown field is not allowed");
                    }
                }
            } else if constexpr (std::same_as<T, Enumeration>) {
                for (const auto& candidate : rule.values)
                    if (equal(value, candidate)) return;
                require(false, "Value is not in the domain enumeration");
            } else if constexpr (std::same_as<T, OneOf>) {
                std::size_t matches = 0;
                for (const auto& alternative : rule.alternatives) {
                    try { match(alternative, value, depth + 1); ++matches; }
                    catch (const Error& error) {
                        if (error.code != ErrorCode::schema_mismatch) throw;
                    }
                    require(matches <= 1, "Value matches more than one schema alternative");
                }
                require(matches == 1, "Value does not match any schema alternative");
            }
        }, node.rule);
        for (const auto& validator : node.validators) {
            step();
            auto result = validator(value);
            if (!result) {
                auto error = std::move(result.error());
                if (!validPointer(error.path))
                    throw std::invalid_argument("Refinement error path must be a valid relative JSON Pointer");
                error.path = path_ + error.path;
                throw error;
            }
        }
    }
};
} // namespace detail

Schema::Schema() : node_(std::make_shared<detail::SchemaNode>(detail::Any{})) {}
Schema::Schema(std::shared_ptr<const detail::SchemaNode> node) : node_(std::move(node)) {}
Schema Schema::any() { return Schema(); }
Schema Schema::null() { return Schema(std::make_shared<detail::SchemaNode>(detail::Null{})); }
Schema Schema::boolean() { return Schema(std::make_shared<detail::SchemaNode>(detail::Boolean{})); }
Schema Schema::integer(std::int64_t minimum, std::int64_t maximum) {
    if (minimum > maximum) throw std::invalid_argument("Schema minimum exceeds maximum");
    return Schema(std::make_shared<detail::SchemaNode>(detail::Integer{minimum, maximum}));
}
Schema Schema::unsigned_integer(std::uint64_t minimum, std::uint64_t maximum) {
    if (minimum > maximum) throw std::invalid_argument("Schema minimum exceeds maximum");
    return Schema(std::make_shared<detail::SchemaNode>(detail::UnsignedInteger{minimum, maximum}));
}
Schema Schema::number(std::optional<double> minimum, std::optional<double> maximum) {
    if ((minimum && !std::isfinite(*minimum)) || (maximum && !std::isfinite(*maximum)) ||
        (minimum && maximum && *minimum > *maximum))
        throw std::invalid_argument("Schema numeric bounds must be finite and ordered");
    return Schema(std::make_shared<detail::SchemaNode>(detail::Number{minimum, maximum}));
}
Schema Schema::string(SizeBounds bounds) {
    detail::checkBounds(bounds);
    return Schema(std::make_shared<detail::SchemaNode>(detail::String{bounds}));
}
Schema Schema::array(Schema element, SizeBounds bounds) {
    detail::checkBounds(bounds);
    return Schema(std::make_shared<detail::SchemaNode>(detail::Array{std::move(element), bounds}));
}
Schema Schema::object(std::vector<Field> fields, AdditionalFields additional) {
    if (additional != AdditionalFields::allow && additional != AdditionalFields::reject)
        throw std::invalid_argument("Invalid additional field policy");
    detail::Object rule{{}, additional};
    for (auto& field : fields) {
        if (!ii::json::detail::valid_utf8(field.name)) throw std::invalid_argument("Schema field names require UTF-8");
        if (field.presence != Presence::required && field.presence != Presence::optional)
            throw std::invalid_argument("Invalid field presence policy");
        auto name = field.name;
        if (!rule.fields.emplace(std::move(name), std::move(field)).second)
            throw std::invalid_argument("Duplicate schema field name");
    }
    return Schema(std::make_shared<detail::SchemaNode>(std::move(rule)));
}
Schema Schema::literal(Value value) {
    std::vector<Value> values;
    values.push_back(std::move(value));
    return enumeration(std::move(values));
}
Schema Schema::enumeration(std::vector<Value> values) {
    if (values.empty()) throw std::invalid_argument("Schema enumeration must not be empty");
    Limits limits;
    limits.string_bytes = std::numeric_limits<std::size_t>::max();
    limits.items = std::numeric_limits<std::size_t>::max();
    limits.depth = detail::maximumDepth;
    const auto json = Schema::any();
    for (const auto& value : values) {
        if (const auto valid = json.validate(value, limits); !valid)
            throw std::invalid_argument("Schema enumeration requires bounded JSON values: " + valid.error().message);
    }
    return Schema(std::make_shared<detail::SchemaNode>(detail::Enumeration{std::move(values)}));
}
Schema Schema::one_of(std::vector<Schema> alternatives) {
    if (alternatives.empty()) throw std::invalid_argument("Schema alternatives must not be empty");
    return Schema(std::make_shared<detail::SchemaNode>(detail::OneOf{std::move(alternatives)}));
}
Schema Schema::nullable() const {
    auto node = std::make_shared<detail::SchemaNode>(*node_);
    node->nullable = true;
    return Schema(std::move(node));
}
Schema Schema::refine(Validator validator) const {
    if (!validator) throw std::invalid_argument("Schema refinement must be callable");
    auto node = std::make_shared<detail::SchemaNode>(*node_);
    node->validators.push_back(std::move(validator));
    return Schema(std::move(node));
}
Result<void> Schema::validate(const Value& value, Limits limits) const {
    return detail::SchemaValidator(limits).run(*this, value);
}
Result<void> Schema::validate(const Value& value, Limits limits, SchemaBudget& budget) const {
    return detail::SchemaValidator(limits, &budget).run(*this, value);
}
SchemaKind Schema::kind() const noexcept { return static_cast<SchemaKind>(node_->rule.index()); }
bool Schema::nullable_enabled() const noexcept { return node_->nullable; }
Field Field::required(std::string name, Schema schema) {
    return Field{std::move(name), std::move(schema), Presence::required};
}
Field Field::optional(std::string name, Schema schema) {
    return Field{std::move(name), std::move(schema), Presence::optional};
}
Result<void> validate(const Value& value, const Schema& schema, Limits limits) {
    return schema.validate(value, limits);
}
Result<Value> detail::parseSchema(std::string_view input, const Schema& schema, Limits limits) {
    auto value = parse(input, limits);
    if (!value) return std::unexpected(value.error());
    if (const auto valid = schema.validate(*value, limits); !valid) return std::unexpected(valid.error());
    return value;
}
Result<std::string> detail::stringifySchema(const Value& value, const Schema& schema, Limits limits) {
    if (const auto valid = schema.validate(value, limits); !valid) return std::unexpected(valid.error());
    return stringify(value, limits);
}
} // namespace ii::json
