#pragma once
#include <iiJson/Value.h>

namespace ii::json {
enum class ErrorCode { syntax, invalid_utf8, duplicate_key, number_out_of_range,
    limit_exceeded, unsupported_value, schema_mismatch };
struct Error final {
    ErrorCode code;
    std::size_t offset=0,line=1,column=1; // UTF-8 bytes, one-based line/column.
    std::string message;
    std::string path; // RFC 6901 JSON Pointer; empty means the root or a text error.
};
template<class T> using Result=std::expected<T,Error>;
struct Limits final {
    std::size_t input_bytes=8*1024*1024,output_bytes=8*1024*1024;
    std::size_t string_bytes=1024*1024,items=100000,depth=64;
    std::size_t schema_steps=100000;
};
// Strict rejection remains the default. Last-wins is an explicit compatibility
// policy for formats that historically accepted duplicate decoded object keys.
enum class DuplicateKeyPolicy { reject, last_wins };
[[nodiscard]] Result<Value> parse(std::string_view input,Limits limits={});
[[nodiscard]] Result<Value> parse_with_duplicate_keys(std::string_view input,
    DuplicateKeyPolicy policy,Limits limits={});
[[nodiscard]] Result<std::string> stringify(const Value& value,Limits limits={});
class Schema;
[[nodiscard]] Result<void> validate(const Value& value,const Schema& schema,Limits limits={});
// Domain modules explicitly specialize Codec<T>; no reflection or coercion.
// An optional static schema() returning Schema or const Schema& validates both
// conversion boundaries. Include Schema.h to define the domain contract.
template<class T> struct Codec;
template<class T> concept Serializable=requires(const T& value,const Value& tree) {
    { Codec<T>::encode(value) } -> std::same_as<Result<Value>>;
    { Codec<T>::decode(tree) } -> std::same_as<Result<T>>;
};
template<Serializable T> [[nodiscard]] Result<T> decode(std::string_view input,Limits limits={}) {
    auto tree=parse(input,limits);
    if (!tree) return std::unexpected(tree.error());
    if constexpr (requires { Codec<T>::schema(); }) {
        auto valid=validate(*tree,Codec<T>::schema(),limits);
        if (!valid) return std::unexpected(valid.error());
    }
    return Codec<T>::decode(*tree);
}
template<Serializable T> [[nodiscard]] Result<std::string> encode(const T& value,Limits limits={}) {
    auto tree=Codec<T>::encode(value);
    if (!tree) return std::unexpected(tree.error());
    if constexpr (requires { Codec<T>::schema(); }) {
        auto valid=validate(*tree,Codec<T>::schema(),limits);
        if (!valid) return std::unexpected(valid.error());
    }
    return stringify(*tree,limits);
}
} // namespace ii::json
