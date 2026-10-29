#pragma once
#include <iiJson/Schema.h>
#include <initializer_list>
#include <algorithm>

namespace ii::json::detail {
[[noreturn]] inline void contractError(std::string path, std::string message,
                                      ErrorCode code = ErrorCode::schema_mismatch) {
    throw Error{code, 0, 1, 1, std::move(message), std::move(path)};
}
inline std::string pointerChild(std::string_view base, std::string_view token) {
    std::string result(base);
    result += '/';
    for (char c : token) {
        if (c == '~') result += "~0";
        else if (c == '/') result += "~1";
        else result += c;
    }
    return result;
}
inline void consume(SchemaBudget& budget, std::string_view path) {
    if (!budget.remaining_steps) contractError(std::string(path), "Contract evaluation limit exceeded", ErrorCode::limit_exceeded);
    --budget.remaining_steps;
}
inline const Value::Object& readObject(const Value& value, std::string_view path) {
    const auto result = value.get<Value::Object>();
    if (!result) contractError(std::string(path), "Expected an object");
    return result->get();
}
inline const Value::Array& readArray(const Value& value, std::string_view path) {
    const auto result = value.get<Value::Array>();
    if (!result) contractError(std::string(path), "Expected an array");
    return result->get();
}
inline const std::string& readText(const Value& value, std::string_view path) {
    const auto result = value.get<std::string>();
    if (!result) contractError(std::string(path), "Expected a string");
    return result->get();
}
inline bool readBool(const Value& value, std::string_view path) {
    const auto result = value.get<bool>();
    if (!result) contractError(std::string(path), "Expected a boolean");
    return result->get();
}
inline std::uint64_t readUnsigned(const Value& value, std::string_view path) {
    if (const auto result = value.get<std::uint64_t>()) return result->get();
    if (const auto result = value.get<std::int64_t>(); result && result->get() >= 0) return std::uint64_t(result->get());
    contractError(std::string(path), "Expected a nonnegative 64-bit integer");
}
inline std::int64_t readSigned(const Value& value, std::string_view path) {
    if (const auto result = value.get<std::int64_t>()) return result->get();
    if (const auto result = value.get<std::uint64_t>(); result && result->get() <= std::uint64_t(INT64_MAX)) return std::int64_t(result->get());
    contractError(std::string(path), "Expected a signed 64-bit integer");
}
inline std::size_t readSize(const Value& value, std::string_view path) {
    const auto result = readUnsigned(value, path);
    if (result > std::numeric_limits<std::size_t>::max()) contractError(std::string(path), "Size exceeds this platform's range");
    return std::size_t(result);
}
inline double readNumber(const Value& value, std::string_view path) {
    if (const auto result = value.get<double>()) return result->get();
    if (const auto result = value.get<std::int64_t>()) {
        const double converted = double(result->get());
        if (converted < 0x1p63 && converted >= -0x1p63 && std::int64_t(converted) == result->get()) return converted;
    }
    if (const auto result = value.get<std::uint64_t>()) {
        const double converted = double(result->get());
        if (converted < 0x1p64 && std::uint64_t(converted) == result->get()) return converted;
    }
    contractError(std::string(path), "Expected an exactly representable finite binary64 bound");
}
class ObjectReader final {
public:
    ObjectReader(const Value& value, std::string path, std::initializer_list<std::string_view> allowed)
        : object_(readObject(value, path)), path_(std::move(path)) {
        for (const auto& [name, child] : object_) {
            (void)child;
            if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
                contractError(pointerChild(path_, name), "Unknown contract field");
        }
    }
    const Value* optional(std::string_view name) const {
        const auto found = object_.find(name);
        return found == object_.end() ? nullptr : &found->second;
    }
    const Value& required(std::string_view name) const {
        const auto* value = optional(name);
        if (!value) contractError(child(name), "Required contract field is missing");
        return *value;
    }
    std::string child(std::string_view name) const { return pointerChild(path_, name); }
private:
    const Value::Object& object_;
    std::string path_;
};
} // namespace ii::json::detail
