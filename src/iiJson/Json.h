#pragma once
#include <iiSDKCore/Core/Value/Value.h>

namespace ii::json {
using Value=core::Value;
enum class ErrorCode { syntax, invalid_utf8, duplicate_key, number_out_of_range,
    limit_exceeded, unsupported_value, schema_mismatch };
struct Error final {
    ErrorCode code;
    std::size_t offset=0,line=1,column=1; // UTF-8 bytes, one-based line/column.
    std::string message;
};
template<class T> using Result=std::expected<T,Error>;
struct Limits final {
    std::size_t input_bytes=8*1024*1024,output_bytes=8*1024*1024;
    std::size_t string_bytes=1024*1024,items=100000,depth=64;
};
[[nodiscard]] Result<Value> parse(std::string_view input,Limits limits={});
[[nodiscard]] Result<std::string> stringify(const Value& value,Limits limits={});
// Domain modules explicitly specialize Codec<T>; no reflection or coercion.
template<class T> struct Codec;
template<class T> concept Serializable=requires(const T& value,const Value& tree) {
    { Codec<T>::encode(value) } -> std::same_as<Result<Value>>;
    { Codec<T>::decode(tree) } -> std::same_as<Result<T>>;
};
template<Serializable T> [[nodiscard]] Result<T> decode(std::string_view input,Limits limits={}) {
    auto tree=parse(input,limits);
    if (!tree) return std::unexpected(tree.error());
    return Codec<T>::decode(*tree);
}
template<Serializable T> [[nodiscard]] Result<std::string> encode(const T& value,Limits limits={}) {
    auto tree=Codec<T>::encode(value);
    if (!tree) return std::unexpected(tree.error());
    return stringify(*tree,limits);
}
} // namespace ii::json
