#include <iiJson/Json.h>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
using namespace ii::json;
namespace {
int checks=0;
void check(bool value) { ++checks; if (!value) throw std::runtime_error("JSON check "+std::to_string(checks)); }
void rejects(std::string_view input,ErrorCode code=ErrorCode::syntax) {
    const auto result=parse(input); check(!result && result.error().code==code);
}
struct Settings final { std::int64_t version=1; std::optional<std::string> title; };
}
template<> struct ii::json::Codec<Settings> final {
    static Result<Value> encode(const Settings& value) {
        Value::Object out{{"schemaVersion",Value(value.version)}};
        if (value.title) out.emplace("title",Value(*value.title));
        return Value(std::move(out));
    }
    static Result<Settings> decode(const Value& value) {
        auto version=value.find("schemaVersion");
        if (!version || !version->get().get<std::int64_t>() || version->get().get<std::int64_t>()->get()!=1)
            return std::unexpected(Error{ErrorCode::schema_mismatch,0,1,1,"Unsupported settings schema"});
        Settings result;
        auto title=value.find("title");
        if (title) {
            auto text=title->get().get<std::string>();
            if (!text) return std::unexpected(Error{ErrorCode::schema_mismatch,0,1,1,"title must be text when present"});
            result.title=text->get();
        }
        return result;
    }
};
int main() {
    for (std::string_view input : {"null","true","false","0","-1","123","1.0","-0.0","1e2","0e9999","5e-324","2.2250738585072014e-308","1.7976931348623157e308","\"한글 🎵\"","[]","{}","[null,false,\"\",[],{}]"}) {
        auto value=parse(input); check(bool(value)); auto output=stringify(*value); check(bool(output)); check(parse(*output).value()==*value);
    }
    check(parse("9223372036854775807")->get<std::int64_t>()->get()==INT64_MAX);
    check(parse("18446744073709551615")->get<std::uint64_t>()->get()==UINT64_MAX);
    check(parse("-9223372036854775808")->get<std::int64_t>()->get()==INT64_MIN);
    check(std::signbit(parse("-0.0")->get<double>()->get()));
    check(stringify(Value(-0.0)).value()=="-0.0");
    rejects("18446744073709551616",ErrorCode::number_out_of_range);
    rejects("-9223372036854775809",ErrorCode::number_out_of_range);
    rejects("1e9999",ErrorCode::number_out_of_range); rejects("1e-9999",ErrorCode::number_out_of_range);
    for (std::string_view input : {"","+1","01","-01",".5","1.","1e","NaN","Infinity","[1,]","{\"a\":1,}","null true","\"x\n\"","\"\\q\"","\"\\ud800\"","\"\\udc00\"","\"\\ud800\\u0041\""}) rejects(input);
    rejects("{\"a\":1,\"\\u0061\":2}",ErrorCode::duplicate_key);
    rejects(std::string("\"\xff\"",3),ErrorCode::invalid_utf8);
    check(parse("\"\\ud83c\\udfb5\"")->get<std::string>()->get()=="🎵");
    check(parse("\"a\\u0000b\"")->get<std::string>()->get()==std::string("a\0b",3));
    auto value=parse("{\"null\":null,\"empty\":\"\",\"array\":[],\"false\":false}").value();
    check(value.find("null")->get().kind()==ii::core::ValueKind::null && !value.find("absent"));
    check(stringify(value).value()=="{\"array\":[],\"empty\":\"\",\"false\":false,\"null\":null}");
    Limits limits; limits.depth=2; check(parse("[[]]",limits).has_value()); check(!parse("[[[]]]",limits));
    limits={}; limits.input_bytes=2; check(!parse("null",limits));
    limits={}; limits.string_bytes=1; check(!parse("\"ab\"",limits)); check(!stringify(Value("ab"),limits));
    limits={}; limits.items=2; check(!parse("[1,2]",limits)); check(!stringify(Value(Value::Array{Value(1),Value(2)}),limits));
    limits={}; limits.output_bytes=3; check(!stringify(Value(),limits));
    check(!stringify(Value(ii::core::Bytes{std::byte{1}})));
    const auto position=parse("[\n true,\n ]").error(); check(position.line==3 && position.offset==10 && position.column==2);
    check(encode(Settings{}).value()=="{\"schemaVersion\":1}");
    check(encode(Settings{1,std::string{}}).value()=="{\"schemaVersion\":1,\"title\":\"\"}");
    check(!decode<Settings>("{\"schemaVersion\":2}") && !decode<Settings>("{\"schemaVersion\":1,\"title\":null}"));
    check(!decode<Settings>("{\"schemaVersion\":1}")->title);
    std::cout << checks << " JSON grammar, bounds and DTO checks passed\n";
}
