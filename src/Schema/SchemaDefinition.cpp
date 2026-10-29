#include <iiJson/Schema.h>
#include "detail/SchemaNode.h"
#include "detail/ContractIO.h"

namespace ii::json::detail {
class SchemaDefinitionCodec final {
public:
    explicit SchemaDefinitionCodec(Limits limits) : limits_(limits), budget_{limits.schema_steps} {}
    Result<Value> encode(const Schema& schema) {
        try {
            auto result = Value(Value::Object{{"format", Value("iiJson.Schema")}, {"formatVersion", Value(1)},
                                               {"schema", write(schema, "/schema", 1)}});
            const auto bounded = stringify(result, limits_);
            if (!bounded) return std::unexpected(bounded.error());
            return result;
        } catch (const Error& error) { return std::unexpected(error); }
    }
    Result<Schema> decode(const Value& value) {
        try {
            auto bounds = limits_;
            bounds.output_bytes = bounds.input_bytes;
            if (const auto bounded = stringify(value, bounds); !bounded) return std::unexpected(bounded.error());
            ObjectReader root(value, "", {"format", "formatVersion", "schema"});
            if (readText(root.required("format"), "/format") != "iiJson.Schema") contractError("/format", "Unknown schema definition format");
            if (readUnsigned(root.required("formatVersion"), "/formatVersion") != 1) contractError("/formatVersion", "Unsupported schema definition version");
            return read(root.required("schema"), "/schema", 1);
        } catch (const Error& error) { return std::unexpected(error); }
    }
private:
    Limits limits_;
    SchemaBudget budget_;
    std::size_t literalBytes_ = 0;
    void enter(std::string_view path, std::size_t depth) {
        consume(budget_, path);
        if (depth > maximumSchemaDepth) contractError(std::string(path), "Schema definition depth exceeded", ErrorCode::limit_exceeded);
    }
    void account(const Value& value, std::string_view path) {
        auto bounds = limits_;
        bounds.output_bytes = limits_.output_bytes - literalBytes_;
        const auto bytes = stringify(value, bounds);
        if (!bytes) { auto error = bytes.error(); error.path = std::string(path); throw error; }
        literalBytes_ += bytes->size();
    }
    Value write(const Schema& schema, const std::string& path, std::size_t depth) {
        enter(path, depth);
        const auto& node = *schema.node_;
        if (!node.validators.empty()) contractError(path, "Native refinement callbacks have no portable definition", ErrorCode::unsupported_value);
        Value::Object out;
        if (node.nullable) out.emplace("nullable", Value(true));
        std::visit([&](const auto& rule) {
            using T = std::decay_t<decltype(rule)>;
            const auto kind = [&](const char* name) { out.emplace("kind", Value(name)); };
            const auto size = [&](SizeBounds bounds, const char* low, const char* high) {
                out.emplace(low, Value(std::uint64_t(bounds.minimum)));
                if (bounds.maximum != std::numeric_limits<std::size_t>::max()) out.emplace(high, Value(std::uint64_t(bounds.maximum)));
            };
            if constexpr (std::same_as<T, Any>) kind("any");
            else if constexpr (std::same_as<T, Null>) kind("null");
            else if constexpr (std::same_as<T, Boolean>) kind("boolean");
            else if constexpr (std::same_as<T, Integer> || std::same_as<T, UnsignedInteger>) {
                kind(std::same_as<T, Integer> ? "integer" : "unsignedInteger");
                out.emplace("minimum", Value(rule.minimum)); out.emplace("maximum", Value(rule.maximum));
            } else if constexpr (std::same_as<T, Number>) {
                kind("number");
                if (rule.minimum) out.emplace("minimum", Value(*rule.minimum));
                if (rule.maximum) out.emplace("maximum", Value(*rule.maximum));
            } else if constexpr (std::same_as<T, String>) {
                kind("string"); size(rule.bounds, "minimumLength", "maximumLength");
            } else if constexpr (std::same_as<T, Array>) {
                kind("array"); size(rule.bounds, "minimumItems", "maximumItems");
                out.emplace("element", write(rule.element, pointerChild(path, "element"), depth + 1));
            } else if constexpr (std::same_as<T, Object>) {
                kind("object"); out.emplace("additional", Value(rule.additional == AdditionalFields::allow ? "allow" : "reject"));
                Value::Array fields;
                for (const auto& [name, field] : rule.fields) {
                    consume(budget_, path);
                    if (name.size() > limits_.string_bytes) contractError(path, "Schema field name exceeds byte limit", ErrorCode::limit_exceeded);
                    account(Value(name), path);
                    const auto fieldPath = pointerChild(pointerChild(path, "fields"), std::to_string(fields.size()));
                    fields.emplace_back(Value::Object{{"name", Value(name)},
                        {"presence", Value(field.presence == Presence::required ? "required" : "optional")},
                        {"schema", write(field.schema, pointerChild(fieldPath, "schema"), depth + 1)}});
                }
                out.emplace("fields", Value(std::move(fields)));
            } else if constexpr (std::same_as<T, Enumeration>) {
                kind("enumeration");
                for (const auto& value : rule.values) { consume(budget_, path); account(value, path); }
                out.emplace("values", Value(rule.values));
            } else if constexpr (std::same_as<T, OneOf>) {
                kind("oneOf"); Value::Array alternatives;
                for (const auto& alternative : rule.alternatives)
                    alternatives.push_back(write(alternative, pointerChild(pointerChild(path, "alternatives"), std::to_string(alternatives.size())), depth + 1));
                out.emplace("alternatives", Value(std::move(alternatives)));
            }
        }, node.rule);
        return Value(std::move(out));
    }
    Schema read(const Value& value, const std::string& path, std::size_t depth) {
        enter(path, depth);
        // Read kind before applying the kind-specific closed set of keywords.
        const auto& object = readObject(value, path);
        const auto found = object.find("kind");
        if (found == object.end()) contractError(pointerChild(path, "kind"), "Schema kind is missing");
        const auto& kind = readText(found->second, pointerChild(path, "kind"));
        const auto construct = [&]() -> Schema {
            if (kind == "any" || kind == "null" || kind == "boolean") {
                ObjectReader rule(value, path, {"kind", "nullable"});
                return kind == "any" ? Schema::any() : kind == "null" ? Schema::null() : Schema::boolean();
            }
            if (kind == "integer" || kind == "unsignedInteger" || kind == "number") {
                ObjectReader rule(value, path, {"kind", "nullable", "minimum", "maximum"});
                const auto* low = rule.optional("minimum"); const auto* high = rule.optional("maximum");
                if (kind == "integer") return Schema::integer(low ? readSigned(*low, rule.child("minimum")) : INT64_MIN,
                                                               high ? readSigned(*high, rule.child("maximum")) : INT64_MAX);
                if (kind == "unsignedInteger") return Schema::unsigned_integer(low ? readUnsigned(*low, rule.child("minimum")) : 0,
                                                                               high ? readUnsigned(*high, rule.child("maximum")) : UINT64_MAX);
                return Schema::number(low ? std::optional(readNumber(*low, rule.child("minimum"))) : std::nullopt,
                                      high ? std::optional(readNumber(*high, rule.child("maximum"))) : std::nullopt);
            }
            if (kind == "string" || kind == "array") {
                const bool string = kind == "string";
                ObjectReader rule(value, path, string ? std::initializer_list<std::string_view>{"kind", "nullable", "minimumLength", "maximumLength"}
                                                      : std::initializer_list<std::string_view>{"kind", "nullable", "element", "minimumItems", "maximumItems"});
                const auto low = string ? "minimumLength" : "minimumItems";
                const auto high = string ? "maximumLength" : "maximumItems";
                SizeBounds bounds;
                if (const auto* entry = rule.optional(low)) bounds.minimum = readSize(*entry, rule.child(low));
                if (const auto* entry = rule.optional(high)) bounds.maximum = readSize(*entry, rule.child(high));
                if (string) return Schema::string(bounds);
                return Schema::array(read(rule.required("element"), rule.child("element"), depth + 1), bounds);
            }
            if (kind == "object") {
                ObjectReader rule(value, path, {"kind", "nullable", "additional", "fields"});
                auto additional = AdditionalFields::reject;
                if (const auto* entry = rule.optional("additional")) {
                    const auto& policy = readText(*entry, rule.child("additional"));
                    if (policy != "allow" && policy != "reject") contractError(rule.child("additional"), "Invalid additional field policy");
                    if (policy == "allow") additional = AdditionalFields::allow;
                }
                std::vector<Field> fields;
                for (const auto& entry : readArray(rule.required("fields"), rule.child("fields"))) {
                    const auto fieldPath = pointerChild(rule.child("fields"), std::to_string(fields.size()));
                    consume(budget_, fieldPath);
                    ObjectReader field(entry, fieldPath, {"name", "presence", "schema"});
                    const auto& presence = readText(field.required("presence"), field.child("presence"));
                    if (presence != "required" && presence != "optional") contractError(field.child("presence"), "Invalid field presence");
                    fields.push_back(Field{readText(field.required("name"), field.child("name")),
                        read(field.required("schema"), field.child("schema"), depth + 1),
                        presence == "required" ? Presence::required : Presence::optional});
                }
                return Schema::object(std::move(fields), additional);
            }
            if (kind == "enumeration") {
                ObjectReader rule(value, path, {"kind", "nullable", "values"});
                return Schema::enumeration(readArray(rule.required("values"), rule.child("values")));
            }
            if (kind == "oneOf") {
                ObjectReader rule(value, path, {"kind", "nullable", "alternatives"});
                std::vector<Schema> alternatives;
                for (const auto& alternative : readArray(rule.required("alternatives"), rule.child("alternatives")))
                    alternatives.push_back(read(alternative, pointerChild(rule.child("alternatives"), std::to_string(alternatives.size())), depth + 1));
                return Schema::one_of(std::move(alternatives));
            }
            contractError(pointerChild(path, "kind"), "Unknown schema kind");
        };
        auto result = [&]() -> Schema {
            try { return construct(); }
            catch (const std::invalid_argument& error) { contractError(path, error.what()); }
        }();
        if (const auto nullable = object.find("nullable"); nullable != object.end() && readBool(nullable->second, pointerChild(path, "nullable")))
            result = result.nullable();
        return result;
    }
};
} // namespace ii::json::detail

namespace ii::json {
Result<Value> Schema::definition(Limits limits) const { return detail::SchemaDefinitionCodec(limits).encode(*this); }
Result<Schema> Schema::from_definition(const Value& value, Limits limits) {
    try { return detail::SchemaDefinitionCodec(limits).decode(value); }
    catch (const std::invalid_argument& error) { return std::unexpected(Error{ErrorCode::schema_mismatch, 0, 1, 1, error.what(), "/schema"}); }
}
} // namespace ii::json
