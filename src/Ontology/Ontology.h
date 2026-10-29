#pragma once
#include <iiJson/Schema.h>
#include <memory>
#include <string>
#include <vector>

namespace ii::json::ontology {
namespace detail { struct CatalogData; struct SnapshotData; }
struct OntologyLimits final {
    Limits json;
    std::size_t definitions = 4096, objects = 10000, links = 20000, work = 1000000;
};
struct ValueType final { std::string id; Schema schema; Value::Object metadata; };
struct PropertyType final { std::string id, value_type; Value::Object metadata; };
struct PropertyBinding final {
    std::string name, property;
    Presence presence = Presence::required;
};
struct InterfaceType final {
    std::string id;
    std::vector<PropertyBinding> properties;
    std::vector<std::string> extends;
    Value::Object metadata;
};
struct ObjectType final {
    std::string id, primary_key;
    std::vector<PropertyBinding> properties;
    std::vector<std::string> interfaces;
    AdditionalFields additional = AdditionalFields::reject;
    Value::Object metadata;
};
enum class SelectorKind { object, interface_type };
struct TypeSelector final { std::string type; SelectorKind kind = SelectorKind::object; };
struct LinkType final {
    std::string id;
    TypeSelector source, target;
    SizeBounds targets_per_source, sources_per_target;
    std::vector<PropertyBinding> properties;
    Value::Object metadata;
};
struct TypeScope final { std::vector<std::string> objects, links; };
struct ActionType final {
    std::string id;
    Schema parameters = Schema::object({}), result = Schema::any();
    TypeScope writes;
    Value::Object metadata;
};
struct FunctionType final {
    std::string id;
    Schema parameters = Schema::object({}), result = Schema::any();
    TypeScope reads;
    Value::Object metadata;
};
struct Definition final {
    std::string id;
    std::uint64_t revision = 1;
    std::vector<ValueType> value_types;
    std::vector<PropertyType> properties;
    std::vector<InterfaceType> interfaces;
    std::vector<ObjectType> objects;
    std::vector<LinkType> links;
    std::vector<ActionType> actions;
    std::vector<FunctionType> functions;
    Value::Object metadata;
};
struct ObjectRef final {
    std::string type;
    Value id;
    bool operator==(const ObjectRef&) const = default;
};
struct Object final { std::string type; Value::Object properties; };
struct Link final { std::string type; ObjectRef source, target; Value::Object properties; };
struct GraphData final {
    std::vector<Object> objects;
    std::vector<Link> links;
    Value::Object metadata;
};
enum class Direction { outgoing, incoming, both };
struct QueryLimits final { std::size_t results = 1000, steps = 100000; };

class Catalog;
class Snapshot final {
public:
    [[nodiscard]] const GraphData& data() const & noexcept;
    const GraphData& data() const && = delete;
    [[nodiscard]] Result<std::reference_wrapper<const Object>> find(const ObjectRef& reference) const &;
    Result<std::reference_wrapper<const Object>> find(const ObjectRef&) const && = delete;
    [[nodiscard]] Result<std::vector<ObjectRef>> neighbors(const ObjectRef& reference,
        std::string_view link_type = {}, Direction direction = Direction::outgoing, QueryLimits limits = {}) const;
    [[nodiscard]] Result<Value> to_value(Limits limits = {}) const;
private:
    explicit Snapshot(std::shared_ptr<const detail::SnapshotData> data);
    std::shared_ptr<const detail::SnapshotData> data_;
    friend class Catalog;
};

// Compiled, immutable semantic contracts. Runtime execution, persistence and
// authorization providers can depend on this layer without a reverse edge.
class Catalog final {
public:
    [[nodiscard]] static Result<Catalog> compile(Definition definition, OntologyLimits limits = {});
    [[nodiscard]] static Result<Catalog> from_value(const Value& manifest, OntologyLimits limits = {});
    [[nodiscard]] Result<Value> to_value(Limits limits = {}) const;
    [[nodiscard]] const Definition& definition() const & noexcept;
    const Definition& definition() const && = delete;
    [[nodiscard]] bool implements(std::string_view object_type, std::string_view interface_id) const noexcept;
    [[nodiscard]] Result<ObjectRef> validate_object(const Object& object, OntologyLimits limits = {}) const;
    [[nodiscard]] Result<void> validate_action(std::string_view action, const Value& arguments, OntologyLimits limits = {}) const;
    [[nodiscard]] Result<void> validate_action_result(std::string_view action, const Value& result, OntologyLimits limits = {}) const;
    [[nodiscard]] Result<void> validate_function(std::string_view function, const Value& arguments, OntologyLimits limits = {}) const;
    [[nodiscard]] Result<void> validate_function_result(std::string_view function, const Value& result, OntologyLimits limits = {}) const;
    [[nodiscard]] Result<Snapshot> snapshot(GraphData data, OntologyLimits limits = {}) const;
    [[nodiscard]] Result<Snapshot> load_snapshot(const Value& document, OntologyLimits limits = {}) const;
private:
    explicit Catalog(std::shared_ptr<const detail::CatalogData> data);
    std::shared_ptr<const detail::CatalogData> data_;
    friend class Snapshot;
};
} // namespace ii::json::ontology

namespace ii::json {
template<> struct Codec<ontology::Catalog> final {
    [[nodiscard]] static Result<Value> encode(const ontology::Catalog& catalog) { return catalog.to_value(); }
    [[nodiscard]] static Result<ontology::Catalog> decode(const Value& value) { return ontology::Catalog::from_value(value); }
};
} // namespace ii::json
