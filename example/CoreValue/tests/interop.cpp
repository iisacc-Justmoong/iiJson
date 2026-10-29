#include <iiJson/CoreValue.h>
#include <iiJson/Json.h>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

int main() {
    using ii::core::Value;
    const Value original(Value::Object{
        {"null",Value()}, {"signed",Value(std::int64_t(-5))},
        {"unsigned",Value(std::uint64_t(18446744073709551615ULL))},
        {"real",Value(-0.25)}, {"text",Value("한글")},
        {"array",Value(Value::Array{Value(true),Value(ii::core::Bytes{std::byte{42}})})}
    });
    auto json=ii::json::interop::fromCore(original);
    const auto converted=ii::json::interop::toCore(json);
    if (!(converted==original)) throw std::runtime_error("Core to JSON to Core must preserve exact types and data");
    if (ii::json::stringify(json)) throw std::runtime_error("binary must still require explicit encoding");
    auto plain=ii::json::parse("{\"nested\":[0,-4,1.0,\"x\"]}").value();
    auto copy=ii::json::interop::fromCore(ii::json::interop::toCore(plain));
    if (!(copy==plain)) throw std::runtime_error("JSON to Core to JSON must preserve exact values");
}
