#include <iiJson/Json.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

using namespace ii::json;
namespace {
void check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
template<class F> void throws(F f, const char* what) {
    try { f(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(what);
}
}
int main() {
    static_assert(!std::is_convertible_v<int, Value>);
    static_assert(!std::is_convertible_v<std::string, Value>);
    check(Value().kind()==ValueKind::null,"null");
    check(Value(-4).kind()==ValueKind::signed_integer,"signed");
    check(Value(4u).kind()==ValueKind::unsigned_integer,"unsigned");
    const Value integer(4);
    check(!integer.get<double>(),"exact numeric extraction");
    throws([] { (void)Value(std::numeric_limits<double>::infinity()); },"infinity");
    throws([] { (void)Value(std::string("\xff",1)); },"invalid text");
    throws([] { (void)Value(Value::Object{{std::string("\xc0\x80",2),Value()}}); },"invalid key");
    const auto tree=Value(Value::Object{{"key",Value(Value::Array{Value(true),Value(2u)})}});
    check(tree.find("key")->get().at(1)->get().get<std::uint64_t>()->get()==2,"nested exact value");
    check(!tree.find("missing") && !tree.at(0),"missing and wrong container");
    check(!stringify(Value(Bytes{std::byte{0xff}})),"binary needs explicit encoding");
    const std::string bad = "[\"" + std::string("\xe2\x28\xa1",3) + "\"]";
    const auto invalid=parse(bad);
    check(!invalid && invalid.error().code==ErrorCode::invalid_utf8 && invalid.error().offset==2,"invalid UTF-8 offset");
    check(parse("\"\\ud83c\\udfb5\"")->get<std::string>()->get()=="🎵","surrogate pair");
    check(!parse("\"\\ud800\\u0041\""),"unpaired surrogate");
    std::uint64_t state=0x4d595df4d0f33173ULL;
    for (int i=0;i<4096;++i) {
        state^=state<<13; state^=state>>7; state^=state<<17;
        double number=std::bit_cast<double>(state);
        if (!std::isfinite(number)) continue;
        const auto output=stringify(Value(number));
        check(bool(output),"finite number encoding");
        const auto decoded=parse(*output);
        check(bool(decoded),"finite number decoding");
        const auto value=decoded->get<double>();
        check(bool(value) && std::bit_cast<std::uint64_t>(value->get())==state,"binary64 exact round trip");
    }
}
