#pragma once
#include <iiJson/Ontology.h>
#include "ContractIO.h"
#include <map>
#include <set>

namespace ii::json::ontology::detail {
namespace io = ii::json::detail;
using Index = std::map<std::string, std::size_t, std::less<>>;
using Bindings = std::map<std::string, PropertyBinding, std::less<>>;
using ObjectKey = std::pair<std::string, std::string>;
struct CompiledObject final {
    Schema properties, key;
    std::set<std::string, std::less<>> interfaces;
};
struct CatalogData final {
    Definition definition;
    Index values, properties, interfaces, objects, links, actions, functions;
    std::vector<Bindings> interface_bindings;
    std::vector<CompiledObject> object_contracts;
    std::vector<Schema> link_contracts;
    Value manifest;
};
struct SnapshotData final {
    std::shared_ptr<const CatalogData> catalog;
    GraphData graph;
    std::map<ObjectKey, std::size_t> index;
    std::vector<ObjectRef> references;
    std::map<ObjectKey, std::vector<std::size_t>> outgoing, incoming;
    Value manifest;
    Limits identity_limits;
};
class State final {
public:
    explicit State(OntologyLimits policy) : limits(policy), budget{policy.work} {}
    OntologyLimits limits;
    SchemaBudget budget;
    std::size_t items = 0, bytes = 0;
    void step(std::string_view path) { io::consume(budget, path); }
    void identifier(std::string_view id, std::string_view path) {
        step(path);
        if (id.empty() || !ii::json::detail::valid_utf8(id)) io::contractError(std::string(path), "Identifier requires nonempty UTF-8");
        for (unsigned char byte : id)
            if (byte <= 0x20 || byte == 0x7f) io::contractError(std::string(path), "Identifier contains whitespace or control bytes");
        text(id, path);
    }
    void text(std::string_view text, std::string_view path) {
        if (text.size() > limits.json.string_bytes || text.size() > limits.json.input_bytes - bytes)
            io::contractError(std::string(path), "Ontology text budget exceeded", ErrorCode::limit_exceeded);
        bytes += text.size();
    }
    void item(std::string_view path, std::size_t depth) {
        step(path);
        if (items >= limits.json.items || depth > limits.json.depth || depth > 256 || limits.json.depth > 256)
            io::contractError(std::string(path), "Ontology document resource limit exceeded", ErrorCode::limit_exceeded);
        ++items;
    }
    void account(const Value& value, std::string_view path, std::size_t depth = 1) {
        item(path, depth);
        if (const auto string = value.get<std::string>()) text(string->get(), path);
        else if (const auto array = value.get<Value::Array>()) {
            for (std::size_t i = 0; i < array->get().size(); ++i) account(array->get()[i], io::pointerChild(path, std::to_string(i)), depth + 1);
        } else if (const auto object = value.get<Value::Object>()) accountObject(object->get(), path, depth);
        else if (value.kind() == ii::json::ValueKind::bytes)
            io::contractError(std::string(path), "Binary value has no ontology JSON encoding", ErrorCode::unsupported_value);
    }
    void accountObject(const Value::Object& object, std::string_view path, std::size_t depth = 1) {
        for (const auto& [key, value] : object) {
            if (!ii::json::detail::valid_utf8(key)) io::contractError(std::string(path), "Ontology property keys require UTF-8", ErrorCode::invalid_utf8);
            const auto child = io::pointerChild(path, key);
            item(child, depth); text(key, child); account(value, child, depth + 1);
        }
    }
    void validate(const Schema& schema, const Value& value, std::string_view prefix) {
        const auto valid = schema.validate(value, limits.json, budget);
        if (!valid) { auto error = valid.error(); error.path = std::string(prefix) + error.path; throw error; }
    }
};
[[nodiscard]] Value encodeDefinition(const Definition& definition, State& state);
[[nodiscard]] Definition decodeDefinition(const Value& value, State& state);
[[nodiscard]] Value encodeGraph(const CatalogData& catalog, const GraphData& graph, State& state);
[[nodiscard]] GraphData decodeGraph(const CatalogData& catalog, const Value& value, State& state);
[[nodiscard]] ObjectKey objectKey(const ObjectRef& ref, std::string_view path, Limits limits);
[[nodiscard]] ObjectRef validateObject(const CatalogData& catalog, const Object& object, State& state, std::string_view path);
[[nodiscard]] bool matches(const CatalogData& catalog, const TypeSelector& selector, std::string_view object_type);
} // namespace ii::json::ontology::detail
