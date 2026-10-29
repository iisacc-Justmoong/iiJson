#include <iiJson/Schema.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>

using namespace ii::json;
namespace {
int checks = 0;
int decodeCalls = 0;
void check(bool condition, std::string_view description) {
    ++checks;
    if (!condition) throw std::runtime_error(std::string(description));
}
void accepts(const Schema& schema, std::string_view input) {
    const auto result = parse(input, schema);
    check(result.has_value(), std::string("Expected acceptance: ") + std::string(input));
}
void rejects(const Schema& schema, std::string_view input, std::string_view path = "",
             ErrorCode code = ErrorCode::schema_mismatch) {
    const auto result = parse(input, schema);
    check(!result, std::string("Expected rejection: ") + std::string(input));
    check(result.error().code == code, "Error code");
    check(result.error().path == path, "Error JSON Pointer");
    check(!result.error().message.empty(), "Error explanation");
}
template<class Function> void invalidDefinition(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Invalid schema definition must be rejected");
}
struct Settings final {
    std::int64_t version = 1;
    std::optional<std::string> title;
};
struct Count final { std::int64_t value = 0; };
std::int64_t signedDomainValue(const Value& value) {
    const auto integer = value.get<std::int64_t>();
    return integer ? integer->get() : std::int64_t(value.get<std::uint64_t>()->get());
}
}

template<> struct ii::json::Codec<Settings> final {
    static const Schema& schema() {
        static const auto definition = Schema::object({
            Field::required("schemaVersion", Schema::literal(Value(1))),
            Field::optional("title", Schema::string({1, 80}))
        });
        return definition;
    }
    static Result<Value> encode(const Settings& settings) {
        Value::Object result{{"schemaVersion", Value(settings.version)}};
        if (settings.title) result.emplace("title", Value(*settings.title));
        return Value(std::move(result));
    }
    static Result<Settings> decode(const Value& value) {
        ++decodeCalls;
        Settings result;
        if (const auto title = value.find("title")) result.title = title->get().get<std::string>()->get();
        return result;
    }
};

template<> struct ii::json::Codec<Count> final {
    static Schema schema() { return Schema::integer(0, 10); }
    static Result<Value> encode(const Count& count) {
        if (count.value == -1)
            return std::unexpected(Error{ErrorCode::schema_mismatch, 7, 2, 3, "Conversion failed", "/internal"});
        return Value(count.value);
    }
    static Result<Count> decode(const Value& value) {
        const auto count = signedDomainValue(value);
        if (count == 7)
            return std::unexpected(Error{ErrorCode::schema_mismatch, 0, 1, 1, "Reserved count"});
        return Count{count};
    }
};

int main() {
    check(parse("null", {}).has_value(), "Existing empty Limits argument remains unambiguous");
    check(stringify(Value(), {}).value() == "null", "Existing stringify Limits argument");
    accepts(Schema::any(), R"({"x":[null,false,1,1.5,""]})");
    accepts(Schema::null(), "null");
    accepts(Schema::boolean(), "false");
    rejects(Schema::boolean(), "0");
    rejects(Schema::null(), "false");
    accepts(Schema::integer(), "9223372036854775807");
    accepts(Schema::integer(), "-9223372036854775808");
    accepts(Schema::unsigned_integer(), "0");
    accepts(Schema::unsigned_integer(), "18446744073709551615");
    check(Schema::integer().validate(Value(std::uint64_t(1))).has_value(), "Positive unsigned integer fits signed domain");
    rejects(Schema::integer(), "9223372036854775808");
    rejects(Schema::unsigned_integer(), "-1");
    rejects(Schema::integer(), "1.0");
    rejects(Schema::integer(), "true");
    rejects(Schema::integer(), R"("1")");
    accepts(Schema::integer(-2, 2), "-2");
    accepts(Schema::integer(-2, 2), "2");
    rejects(Schema::integer(-2, 2), "3");
    rejects(Schema::unsigned_integer(2, 5), "1");
    accepts(Schema::number(-0.5, 2.5), "2");
    accepts(Schema::number(-0.5, 2.5), "2.5");
    rejects(Schema::number(-0.5, 2.5), "3");
    rejects(Schema::number(-0.5, 2.5), "-1");
    rejects(Schema::number(), "false");
    // Comparisons must not round 64-bit integer values to binary64.
    rejects(Schema::number(0x1p63), "9223372036854775807");
    accepts(Schema::number(std::nullopt, 0x1p63), "9223372036854775807");
    rejects(Schema::number(0x1p64), "18446744073709551615");
    accepts(Schema::number(std::nullopt, 0x1p64), "18446744073709551615");
    rejects(Schema::number(std::nullopt, -0.5), "0");
    rejects(Schema::number(0.5), "0");
    accepts(Schema::number(std::nullopt, -0.5), "-1");
    rejects(Schema::number(std::nullopt, -0x1p63 - 2048.0), "-9223372036854775808");

    accepts(Schema::string({2, 2}), R"("한🎵")");
    accepts(Schema::string({2, 2}), R"("e\u0301")");
    accepts(Schema::string({3, 3}), R"("a\u0000b")");
    rejects(Schema::string({1, 80}), R"("")");
    rejects(Schema::string({0, 1}), R"("ab")");
    accepts(Schema::array(Schema::boolean(), {1, 2}), "[true,false]");
    rejects(Schema::array(Schema::boolean(), {1, 2}), "[]");
    rejects(Schema::array(Schema::boolean(), {1, 2}), "[true,false,true]");
    rejects(Schema::array(Schema::boolean()), "[true,0]", "/1");

    const auto track = Schema::object({
        Field::required("name", Schema::string({1, 80})),
        Field::optional("muted", Schema::boolean()),
        Field::optional("comment", Schema::string().nullable())
    });
    const auto project = Schema::object({
        Field::required("schemaVersion", Schema::literal(Value(1))),
        Field::required("tracks", Schema::array(track)),
        Field::optional("state", Schema::enumeration({Value("draft"), Value("published")}))
    });
    accepts(project, R"({"schemaVersion":1,"tracks":[{"name":"Main","comment":null}]})");
    accepts(project, R"({"schemaVersion":1,"tracks":[],"state":"draft"})");
    rejects(project, R"({"tracks":[]})", "/schemaVersion");
    rejects(project, R"({"schemaVersion":2,"tracks":[]})", "/schemaVersion");
    rejects(project, R"({"schemaVersion":1,"tracks":[{}]})", "/tracks/0/name");
    rejects(project, R"({"schemaVersion":1,"tracks":[{"name":"Main","muted":null}]})", "/tracks/0/muted");
    rejects(project, R"({"schemaVersion":1,"tracks":[],"state":"unknown"})", "/state");
    rejects(track, R"({"name":"Main","extra":1})", "/extra");
    rejects(track, "[]");
    accepts(Schema::object({}, AdditionalFields::allow), R"({"extra":{"nested":[1]}})");
    rejects(Schema::object({}), R"({"":true})", "/");
    const auto escaped = Schema::object({Field::required("a~/b", Schema::array(Schema::integer()))});
    rejects(escaped, R"({"a~/b":["wrong"]})", "/a~0~1b/0");
    rejects(escaped, "{}", "/a~0~1b");
    const auto korean = Schema::object({Field::required("이름", Schema::string())});
    rejects(korean, "{}", "/이름");

    const auto text = Schema::string();
    const auto nullable = text.nullable();
    accepts(nullable, "null");
    rejects(text, "null");
    rejects(nullable, "0");
    check(Schema::literal(Value(std::uint64_t(1))).validate(Value(1)).has_value(), "Literal positive integer storage equivalence");
    rejects(Schema::literal(Value(1)), "1.0");
    accepts(Schema::literal(Value(Value::Array{Value(1)})), "[1]");
    rejects(Schema::literal(Value(Value::Array{Value(1)})), "[2]");
    accepts(Schema::literal(Value(Value::Object{{"id", Value(std::uint64_t(1))}})), R"({"id":1})");
    rejects(Schema::literal(Value(Value::Object{{"id", Value(1)}})), R"({"other":1})");
    accepts(Schema::one_of({Schema::string(), Schema::integer()}), R"("choice")");
    accepts(Schema::one_of({Schema::string(), Schema::integer()}), "1");
    rejects(Schema::one_of({Schema::string(), Schema::integer()}), "false");
    rejects(Schema::one_of({Schema::integer(), Schema::number()}), "1");
    accepts(Schema::one_of({Schema::integer(), Schema::number()}), "1.5");
    accepts(Schema::one_of({
        Schema::object({Field::required("value", Schema::integer())}),
        Schema::object({Field::required("value", Schema::string())})
    }), R"({"value":"second branch"})");

    const auto positive = Schema::integer().refine([](const Value& value) -> Result<void> {
        if (signedDomainValue(value) <= 0)
            return std::unexpected(Error{ErrorCode::schema_mismatch, 0, 1, 1, "Must be positive"});
        return {};
    });
    accepts(positive, "1");
    check(positive.validate(Value(std::uint64_t(1))).has_value(), "Refinement handles accepted integer storage variants");
    rejects(Schema::object({Field::required("count", positive)}), R"({"count":0})", "/count");
    rejects(positive, R"("wrong")"); // Refinement runs only after the base type succeeds.
    const auto ordered = Schema::object({
        Field::required("start", Schema::integer()), Field::required("end", Schema::integer())
    }).refine([](const Value& value) -> Result<void> {
        const auto start = signedDomainValue(value.find("start")->get());
        const auto end = signedDomainValue(value.find("end")->get());
        if (start > end)
            return std::unexpected(Error{ErrorCode::schema_mismatch, 0, 1, 1, "end precedes start", "/end"});
        return {};
    });
    rejects(Schema::object({Field::required("range", ordered)}), R"({"range":{"start":2,"end":1}})", "/range/end");
    const auto original = Schema::integer();
    const auto never = original.refine([](const Value&) -> Result<void> {
        return std::unexpected(Error{ErrorCode::schema_mismatch, 0, 1, 1, "Always rejected"});
    });
    accepts(original, "1");
    rejects(never, "1");
    const auto fatal = Schema::any().refine([](const Value&) -> Result<void> {
        return std::unexpected(Error{ErrorCode::limit_exceeded, 0, 1, 1, "Domain limit"});
    });
    rejects(Schema::one_of({fatal, Schema::any()}), "1", "", ErrorCode::limit_exceeded);

    Limits limits;
    limits.depth = 2;
    check(Schema::any().validate(Value(Value::Array{Value(Value::Array{})}), limits).has_value(), "Depth limit includes its endpoint");
    check(!Schema::any().validate(Value(Value::Array{Value(Value::Array{Value()})}), limits), "Any obeys depth limits");
    limits = {}; limits.items = 2;
    check(!Schema::any().validate(Value(Value::Array{Value(1), Value(2)}), limits), "Any obeys item limits");
    limits = {}; limits.string_bytes = 1;
    const auto longKey = Schema::any().validate(Value(Value::Object{{"ab", Value(1)}}), limits);
    check(!longKey && longKey.error().code == ErrorCode::limit_exceeded && longKey.error().path == "/ab", "Object key byte limits");
    limits = {}; limits.depth = 257;
    check(!Schema::any().validate(Value(), limits), "Hard depth ceiling");
    limits = {}; limits.schema_steps = 2;
    const auto work = Schema::one_of({Schema::null(), Schema::boolean(), Schema::string()}).validate(Value("ok"), limits);
    check(!work && work.error().code == ErrorCode::limit_exceeded, "Alternative evaluation budget");
    const auto bytes = Value(Value::Object{{"extra", Value(ii::json::Bytes{std::byte{1}})}});
    const auto invalidJson = Schema::object({}, AdditionalFields::allow).validate(bytes);
    check(!invalidJson && invalidJson.error().code == ErrorCode::unsupported_value && invalidJson.error().path == "/extra", "Allowed unknown fields cannot bypass JSON restrictions");
    limits = {}; limits.schema_steps = 0;
    check(!Schema::any().validate(Value(), limits), "Zero schema evaluation budget");
    limits = {}; limits.input_bytes = 1;
    check(!parse("true", Schema::boolean(), limits), "Schema parsing preserves input byte limits");
    limits = {}; limits.output_bytes = 1;
    check(!stringify(Value(true), Schema::boolean(), limits), "Schema writing preserves output byte limits");
    auto recursive = Schema::any();
    for (int level = 0; level < 256; ++level) recursive = Schema::one_of({recursive});
    const auto deepSchema = recursive.validate(Value());
    check(!deepSchema && deepSchema.error().code == ErrorCode::limit_exceeded, "Schema recursion hard ceiling");

    const auto good = parse(R"({"name":"Main"})", track);
    check(good.has_value(), "Schema parse");
    check(stringify(*good, track).value() == R"({"name":"Main"})", "Schema stringify");
    check(!stringify(Value(Value::Object{}), track), "Stringify rejects invalid domain values");
    const auto syntax = parse("[", track);
    check(!syntax && syntax.error().code == ErrorCode::syntax && syntax.error().offset == 1, "Syntax diagnostics retain source position");
    check(!decode<Settings>(R"({"schemaVersion":2})") && decodeCalls == 0, "Schema rejects before DTO decoding");
    check(!decode<Settings>(R"({"schemaVersion":1,"title":null})") && decodeCalls == 0, "Optional is distinct from nullable");
    check(decode<Settings>(R"({"schemaVersion":1})").has_value() && decodeCalls == 1, "Schema-bound DTO decode");
    check(encode(Settings{1, "Title"}).value() == R"({"schemaVersion":1,"title":"Title"})", "Schema-bound DTO encode");
    const auto badEncode = encode(Settings{2, "Title"});
    check(!badEncode && badEncode.error().path == "/schemaVersion", "Schema validates encoded DTOs");
    check(!encode(Settings{1, ""}), "Encoded field constraints");
    check(encode(Count{2}).value() == "2", "Schema provider can return by value");
    check(decode<Count>("2")->value == 2, "Schema temporary lifetime in decode");
    check(!decode<Count>("11"), "By-value schema rejection");
    check(!decode<Count>("7"), "DTO decoding errors propagate after schema validation");
    const auto conversionFailure = encode(Count{-1});
    check(!conversionFailure && conversionFailure.error().offset == 7 && conversionFailure.error().path == "/internal", "DTO encoding errors retain diagnostics");

    invalidDefinition([] { (void)Schema::integer(2, 1); });
    invalidDefinition([] { (void)Schema::unsigned_integer(2, 1); });
    invalidDefinition([] { (void)Schema::number(2.0, 1.0); });
    invalidDefinition([] { (void)Schema::number(std::numeric_limits<double>::infinity()); });
    invalidDefinition([] { (void)Schema::number(std::nan("")); });
    invalidDefinition([] { (void)Schema::string({2, 1}); });
    invalidDefinition([] { (void)Schema::array(Schema::any(), {2, 1}); });
    invalidDefinition([] { (void)Schema::object({Field::required("same", Schema::any()), Field::optional("same", Schema::any())}); });
    invalidDefinition([] { (void)Schema::object({Field::required(std::string(1, '\xff'), Schema::any())}); });
    invalidDefinition([] { (void)Schema::enumeration({}); });
    invalidDefinition([] { (void)Schema::literal(Value(ii::json::Bytes{})); });
    invalidDefinition([] { (void)Schema::one_of({}); });
    invalidDefinition([] { (void)Schema::any().refine({}); });
    invalidDefinition([] { (void)Schema::object({}, static_cast<AdditionalFields>(-1)); });
    invalidDefinition([] { (void)Schema::object({Field{"field", Schema::any(), static_cast<Presence>(-1)}}); });
    for (const auto& path : {std::string("field"), std::string("/bad~"), std::string("/bad~2"), std::string("/\xff", 2)}) {
        invalidDefinition([&] {
            const auto schema = Schema::any().refine([&](const Value&) -> Result<void> {
                return std::unexpected(Error{ErrorCode::schema_mismatch, 0, 1, 1, "Invalid callback pointer", path});
            });
            (void)schema.validate(Value());
        });
    }
    std::cout << checks << " domain schema checks passed\n";
}
