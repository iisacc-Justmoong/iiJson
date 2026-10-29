#pragma once
#include <iiJson/Value.h>
#include <iiSDKCore/Core/Value/Value.h>
#include <type_traits>

// This application-owned adapter performs an explicit deep copy. Neither SDK
// owns or imports the other SDK's value type.
namespace ii::json::interop {
[[nodiscard]] inline Value fromCore(const core::Value& source) {
    return std::visit([](const auto& value) -> Value {
        using T=std::decay_t<decltype(value)>;
        if constexpr (std::same_as<T,std::monostate>) return Value();
        else if constexpr (std::same_as<T,core::Value::Array>) {
            Value::Array output;
            output.reserve(value.size());
            for (const auto& child:value) output.push_back(fromCore(child));
            return Value(std::move(output));
        } else if constexpr (std::same_as<T,core::Value::Object>) {
            Value::Object output;
            for (const auto& [key,child]:value) output.emplace(key,fromCore(child));
            return Value(std::move(output));
        } else return Value(value);
    },source.storage());
}
[[nodiscard]] inline core::Value toCore(const Value& source) {
    return std::visit([](const auto& value) -> core::Value {
        using T=std::decay_t<decltype(value)>;
        if constexpr (std::same_as<T,std::monostate>) return core::Value();
        else if constexpr (std::same_as<T,Value::Array>) {
            core::Value::Array output;
            output.reserve(value.size());
            for (const auto& child:value) output.push_back(toCore(child));
            return core::Value(std::move(output));
        } else if constexpr (std::same_as<T,Value::Object>) {
            core::Value::Object output;
            for (const auto& [key,child]:value) output.emplace(key,toCore(child));
            return core::Value(std::move(output));
        } else return core::Value(value);
    },source.storage());
}
} // namespace ii::json::interop
