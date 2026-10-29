#include <iiJson/Schema.h>
#include <iostream>
#include <stdexcept>

using namespace ii::json;
namespace {
int checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
}
int main() {
    const std::vector<Schema> definitions{
        Schema::any(), Schema::null(), Schema::boolean(), Schema::integer(-2, 8),
        Schema::unsigned_integer(0, UINT64_MAX), Schema::number(-1.5, 2.5),
        Schema::string({1, 80}).nullable(), Schema::array(Schema::integer(), {0, 4}),
        Schema::object({Field::required("a~/b", Schema::boolean()), Field::optional("note", Schema::string())}),
        Schema::enumeration({Value("draft"), Value("published")}),
        Schema::one_of({Schema::string(), Schema::integer()})
    };
    for (const auto& schema : definitions) {
        auto definition = schema.definition();
        check(definition.has_value(), "Export schema");
        auto json = stringify(*definition);
        check(json.has_value(), "Export JSON");
        auto restored = decode<Schema>(*json);
        check(restored.has_value(), "Import schema");
        check(restored->kind() == schema.kind(), "Kind round trip");
        check(restored->nullable_enabled() == schema.nullable_enabled(), "Null modifier round trip");
        check(encode(*restored).value() == *json, "Stable definition JSON");
        for (const auto& text : {"null", "true", "0", "9", "1.5", "\"draft\"", "[]", "{}"}) {
            const auto value = parse(text).value();
            check(bool(schema.validate(value)) == bool(restored->validate(value)), "Validation semantics round trip");
        }
    }
    auto unknown = parse(R"({"format":"iiJson.Schema","formatVersion":2,"schema":{"kind":"any"}})").value();
    check(!Schema::from_definition(unknown), "Unknown definition version");
    auto typo = parse(R"({"format":"iiJson.Schema","formatVersion":1,"schema":{"kind":"string","minimunLength":1}})").value();
    const auto typoError = Schema::from_definition(typo);
    check(!typoError && typoError.error().path == "/schema/minimunLength", "Unknown keyword rejected with pointer");
    const auto refined = Schema::integer().refine([](const Value&) -> Result<void> { return {}; });
    check(!refined.definition() && refined.definition().error().code == ErrorCode::unsupported_value, "No silent callback loss");
    check(!Schema::array(refined).definition(), "Nested callbacks cannot disappear");
    auto unbounded = Schema::string().definition().value();
    check(!unbounded.find("schema")->get().find("maximumLength"), "Unbounded length is independent of size_t width");
    auto invalidRange = parse(R"({"format":"iiJson.Schema","formatVersion":1,"schema":{"kind":"integer","minimum":2,"maximum":1}})").value();
    check(!Schema::from_definition(invalidRange), "Invalid definition bounds are expected errors");
    auto nestedRange = parse(R"({"format":"iiJson.Schema","formatVersion":1,"schema":{"kind":"array","element":{"kind":"integer","minimum":2,"maximum":1}}})").value();
    const auto nestedError = Schema::from_definition(nestedRange);
    check(!nestedError && nestedError.error().path == "/schema/element", "Invalid nested definitions retain their node pointer");
    auto lossyBound = parse(R"({"format":"iiJson.Schema","formatVersion":1,"schema":{"kind":"number","minimum":9223372036854775807}})").value();
    check(!Schema::from_definition(lossyBound), "Integer definition bounds cannot silently round");
    auto numericBound = parse(R"({"format":"iiJson.Schema","formatVersion":1,"schema":{"kind":"number","minimum":2}})").value();
    check(Schema::from_definition(numericBound)->validate(Value(2)).has_value(), "Exact integral numeric bound");
    Limits limits; limits.schema_steps = 0;
    check(!Schema::any().definition(limits), "Definition export work limit");
    limits = {}; limits.input_bytes = 4;
    check(!Schema::from_definition(unbounded, limits), "Definition import input limit");
    limits = {}; limits.output_bytes = 4;
    check(!Schema::any().definition(limits), "Definition export output limit");
    auto deep = Schema::any();
    for (int i = 0; i < 260; ++i) deep = Schema::array(deep);
    check(!deep.definition(), "Definition export recursion guard");
    SchemaBudget budget{2};
    const auto boolean = Schema::boolean();
    check(boolean.validate(Value(true), {}, budget).has_value(), "Shared budget first use");
    check(boolean.validate(Value(false), {}, budget).has_value(), "Shared budget second use");
    check(!boolean.validate(Value(true), {}, budget) && budget.remaining_steps == 0, "Shared budget is cumulative");
    std::cout << checks << " portable schema checks passed\n";
}
