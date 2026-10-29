#include <iiJson/Ontology.h>
#include "detail/OntologyData.h"
#include <algorithm>
#include <functional>
#include <tuple>

namespace ii::json::ontology {
namespace {
namespace io = ii::json::detail;
using detail::State;
using detail::Bindings;
using detail::CatalogData;
using detail::Index;
template<class T> void indexTypes(const std::vector<T>& values, Index& index, State& state, std::string_view path, std::size_t& count) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto at = io::pointerChild(io::pointerChild(path, std::to_string(i)), "id");
        if (++count > state.limits.definitions) io::contractError(at, "Ontology definition count exceeded", ErrorCode::limit_exceeded);
        state.identifier(values[i].id, at);
        if (!index.emplace(values[i].id, i).second) io::contractError(at, "Duplicate ontology type identifier");
    }
}
std::size_t resolve(const Index& index, std::string_view id, std::string_view path) {
    const auto found = index.find(id);
    if (found == index.end()) io::contractError(std::string(path), "Unknown ontology type reference: " + std::string(id));
    return found->second;
}
Bindings bindings(const CatalogData& data, const std::vector<PropertyBinding>& properties, State& state, std::string_view path) {
    Bindings result;
    for (std::size_t i = 0; i < properties.size(); ++i) {
        const auto at = io::pointerChild(path, std::to_string(i));
        const auto& binding = properties[i];
        state.step(at);
        if (!ii::json::detail::valid_utf8(binding.name)) io::contractError(io::pointerChild(at, "name"), "Property name requires UTF-8");
        state.text(binding.name, io::pointerChild(at, "name"));
        if (binding.presence != Presence::required && binding.presence != Presence::optional) io::contractError(at, "Invalid property presence");
        (void)resolve(data.properties, binding.property, io::pointerChild(at, "property"));
        if (!result.emplace(binding.name, binding).second) io::contractError(at, "Duplicate property binding name");
    }
    return result;
}
void merge(Bindings& target, const Bindings& inherited, State& state, std::string_view path, bool declaration) {
    for (const auto& [name, binding] : inherited) {
        state.step(path);
        const auto found = target.find(name);
        if (found == target.end()) target.emplace(name, binding);
        else {
            if (found->second.property != binding.property) io::contractError(std::string(path), "Interface property semantic types conflict: " + name);
            if (declaration && found->second.presence == Presence::required && binding.presence == Presence::optional)
                io::contractError(std::string(path), "Interface declaration weakens a required inherited property: " + name);
            if (binding.presence == Presence::required) found->second.presence = Presence::required;
        }
    }
}
const Schema& propertySchema(const CatalogData& data, const PropertyBinding& property) {
    const auto& shared = data.definition.properties[data.properties.at(property.property)];
    return data.definition.value_types[data.values.at(shared.value_type)].schema;
}
Schema objectSchema(const CatalogData& data, const Bindings& properties, AdditionalFields additional) {
    std::vector<Field> fields;
    for (const auto& [name, property] : properties) fields.push_back({name, propertySchema(data, property), property.presence});
    return Schema::object(std::move(fields), additional);
}
void selector(const CatalogData& data, const TypeSelector& selector, std::string_view path) {
    if (selector.kind == SelectorKind::object) (void)resolve(data.objects, selector.type, path);
    else if (selector.kind == SelectorKind::interface_type) (void)resolve(data.interfaces, selector.type, path);
    else io::contractError(std::string(path), "Invalid type selector kind");
}
void scope(const CatalogData& data, const TypeScope& scope, State& state, std::string_view path) {
    for (const auto& [values, index, name] : {
         std::tuple{&scope.objects, &data.objects, "objects"}, std::tuple{&scope.links, &data.links, "links"}}) {
        std::set<std::string, std::less<>> seen;
        for (std::size_t i = 0; i < values->size(); ++i) {
            const auto at = io::pointerChild(io::pointerChild(path, name), std::to_string(i));
            state.step(at); (void)resolve(*index, (*values)[i], at);
            if (!seen.insert((*values)[i]).second) io::contractError(at, "Duplicate capability scope reference");
        }
    }
}
}

Catalog::Catalog(std::shared_ptr<const detail::CatalogData> data) : data_(std::move(data)) {}
const Definition& Catalog::definition() const & noexcept { return data_->definition; }
Result<Catalog> Catalog::compile(Definition definition, OntologyLimits limits) {
    try {
        auto data = std::make_shared<CatalogData>();
        data->definition = std::move(definition);
        auto& d = data->definition;
        State state(limits);
        state.identifier(d.id, "/id");
        if (!d.revision) io::contractError("/revision", "Ontology revision must be positive");
        std::size_t count = 0;
        indexTypes(d.value_types, data->values, state, "/valueTypes", count);
        indexTypes(d.properties, data->properties, state, "/properties", count);
        indexTypes(d.interfaces, data->interfaces, state, "/interfaces", count);
        indexTypes(d.objects, data->objects, state, "/objects", count);
        indexTypes(d.links, data->links, state, "/links", count);
        indexTypes(d.actions, data->actions, state, "/actions", count);
        indexTypes(d.functions, data->functions, state, "/functions", count);
        for (std::size_t i = 0; i < d.properties.size(); ++i)
            (void)resolve(data->values, d.properties[i].value_type, "/properties/" + std::to_string(i) + "/valueType");

        data->interface_bindings.resize(d.interfaces.size());
        std::vector<unsigned char> colors(d.interfaces.size());
        std::vector<std::set<std::string, std::less<>>> closures(d.interfaces.size());
        std::function<void(std::size_t, std::size_t)> visit = [&](std::size_t index, std::size_t depth) {
            const auto path = "/interfaces/" + std::to_string(index);
            state.step(path);
            if (depth > 256) io::contractError(path, "Interface inheritance depth exceeded", ErrorCode::limit_exceeded);
            if (colors[index] == 1) io::contractError(path + "/extends", "Interface inheritance cycle");
            if (colors[index] == 2) return;
            colors[index] = 1;
            const auto& iface = d.interfaces[index];
            std::set<std::string> seen;
            for (std::size_t i = 0; i < iface.extends.size(); ++i) {
                const auto at = path + "/extends/" + std::to_string(i);
                state.step(at);
                const auto parent = resolve(data->interfaces, iface.extends[i], at);
                if (!seen.insert(iface.extends[i]).second) io::contractError(at, "Duplicate parent interface");
                visit(parent, depth + 1);
                merge(data->interface_bindings[index], data->interface_bindings[parent], state, at, false);
                closures[index].insert(d.interfaces[parent].id);
                for (const auto& ancestor : closures[parent]) { state.step(at); closures[index].insert(ancestor); }
            }
            merge(data->interface_bindings[index], bindings(*data, iface.properties, state, path + "/properties"), state, path + "/properties", true);
            colors[index] = 2;
        };
        for (std::size_t i = 0; i < d.interfaces.size(); ++i) visit(i, 1);

        for (std::size_t i = 0; i < d.objects.size(); ++i) {
            const auto path = "/objects/" + std::to_string(i);
            const auto& object = d.objects[i];
            auto own = bindings(*data, object.properties, state, path + "/properties");
            const auto key = own.find(object.primary_key);
            if (key == own.end() || key->second.presence != Presence::required) io::contractError(path + "/primaryKey", "Primary key must name a required property");
            const auto& keySchema = propertySchema(*data, key->second);
            if (keySchema.nullable_enabled() || (keySchema.kind() != SchemaKind::string && keySchema.kind() != SchemaKind::integer && keySchema.kind() != SchemaKind::unsigned_integer))
                io::contractError(path + "/primaryKey", "Primary key requires a non-nullable string or integer value type");
            std::set<std::string, std::less<>> implemented;
            std::set<std::string> seen;
            for (std::size_t j = 0; j < object.interfaces.size(); ++j) {
                const auto at = path + "/interfaces/" + std::to_string(j);
                state.step(at);
                const auto interfaceIndex = resolve(data->interfaces, object.interfaces[j], at);
                if (!seen.insert(object.interfaces[j]).second) io::contractError(at, "Duplicate implemented interface");
                implemented.insert(object.interfaces[j]);
                for (const auto& ancestor : closures[interfaceIndex]) { state.step(at); implemented.insert(ancestor); }
                for (const auto& [name, required] : data->interface_bindings[interfaceIndex]) {
                    state.step(at);
                    const auto actual = own.find(name);
                    if (actual == own.end() || actual->second.property != required.property ||
                        (required.presence == Presence::required && actual->second.presence != Presence::required))
                        io::contractError(at, "Object does not implement the interface property contract: " + name);
                }
            }
            data->object_contracts.push_back({objectSchema(*data, own, object.additional), keySchema, std::move(implemented)});
        }
        for (std::size_t i = 0; i < d.links.size(); ++i) {
            const auto path = "/links/" + std::to_string(i);
            const auto& link = d.links[i];
            state.step(path); selector(*data, link.source, path + "/source"); selector(*data, link.target, path + "/target");
            if (link.targets_per_source.minimum > link.targets_per_source.maximum || link.sources_per_target.minimum > link.sources_per_target.maximum)
                io::contractError(path, "Link cardinality minimum exceeds maximum");
            data->link_contracts.push_back(objectSchema(*data, bindings(*data, link.properties, state, path + "/properties"), AdditionalFields::reject));
        }
        for (std::size_t i = 0; i < d.actions.size(); ++i) scope(*data, d.actions[i].writes, state, "/actions/" + std::to_string(i) + "/writes");
        for (std::size_t i = 0; i < d.functions.size(); ++i) scope(*data, d.functions[i].reads, state, "/functions/" + std::to_string(i) + "/reads");
        data->manifest = detail::encodeDefinition(d, state);
        state.validate(Schema::any(), data->manifest, "");
        if (const auto bounded = stringify(data->manifest, limits.json); !bounded) return std::unexpected(bounded.error());
        return Catalog(std::move(data));
    } catch (const Error& error) { return std::unexpected(error); }
      catch (const std::invalid_argument& error) { return std::unexpected(Error{ErrorCode::schema_mismatch, 0, 1, 1, error.what()}); }
}
bool Catalog::implements(std::string_view object, std::string_view interface_id) const noexcept {
    const auto found = data_->objects.find(object);
    return found != data_->objects.end() && data_->object_contracts[found->second].interfaces.contains(interface_id);
}
Result<Value> Catalog::to_value(Limits limits) const {
    if (const auto valid = Schema::any().validate(data_->manifest, limits); !valid) return std::unexpected(valid.error());
    if (const auto bounded = stringify(data_->manifest, limits); !bounded) return std::unexpected(bounded.error());
    return data_->manifest;
}
Result<Catalog> Catalog::from_value(const Value& manifest, OntologyLimits limits) {
    try {
        State state(limits);
        state.validate(Schema::any(), manifest, "");
        auto bounds = limits.json; bounds.output_bytes = bounds.input_bytes;
        if (const auto bounded = stringify(manifest, bounds); !bounded) return std::unexpected(bounded.error());
        auto definition = detail::decodeDefinition(manifest, state);
        limits.work = state.budget.remaining_steps;
        return compile(std::move(definition), limits);
    } catch (const Error& error) { return std::unexpected(error); }
}
Result<ObjectRef> Catalog::validate_object(const Object& object, OntologyLimits limits) const {
    try { State state(limits); return detail::validateObject(*data_, object, state, ""); }
    catch (const Error& error) { return std::unexpected(error); }
}
namespace {
template<class Callable> Result<void> validateCall(const std::vector<Callable>& definitions, const Index& index,
    std::string_view name, const Value& value, bool output, OntologyLimits limits, std::string_view category) {
    try {
        State state(limits);
        const auto found = resolve(index, name, category);
        state.account(value, output ? "/result" : "/arguments");
        state.validate(output ? definitions[found].result : definitions[found].parameters, value, output ? "/result" : "/arguments");
        return {};
    } catch (const Error& error) { return std::unexpected(error); }
}
}
Result<void> Catalog::validate_action(std::string_view name, const Value& value, OntologyLimits limits) const { return validateCall(data_->definition.actions, data_->actions, name, value, false, limits, "/action"); }
Result<void> Catalog::validate_action_result(std::string_view name, const Value& value, OntologyLimits limits) const { return validateCall(data_->definition.actions, data_->actions, name, value, true, limits, "/action"); }
Result<void> Catalog::validate_function(std::string_view name, const Value& value, OntologyLimits limits) const { return validateCall(data_->definition.functions, data_->functions, name, value, false, limits, "/function"); }
Result<void> Catalog::validate_function_result(std::string_view name, const Value& value, OntologyLimits limits) const { return validateCall(data_->definition.functions, data_->functions, name, value, true, limits, "/function"); }
} // namespace ii::json::ontology

namespace ii::json::ontology::detail {
ObjectKey objectKey(const ObjectRef& ref, std::string_view path, Limits limits) {
    Value id = ref.id;
    if (const auto unsignedId = id.get<std::uint64_t>(); unsignedId && unsignedId->get() <= std::uint64_t(INT64_MAX)) id = Value(std::int64_t(unsignedId->get()));
    if (id.kind() != ii::json::ValueKind::text && id.kind() != ii::json::ValueKind::signed_integer && id.kind() != ii::json::ValueKind::unsigned_integer)
        io::contractError(std::string(path), "Object identity requires a string or integer");
    if (const auto text = id.get<std::string>(); text && text->get().empty()) io::contractError(std::string(path), "Object identity cannot be empty");
    const auto encoded = stringify(id, limits);
    if (!encoded) { auto error = encoded.error(); error.path = std::string(path); throw error; }
    return {ref.type, *encoded};
}
ObjectRef validateObject(const CatalogData& data, const Object& object, State& state, std::string_view path) {
    state.step(path);
    const auto found = data.objects.find(object.type);
    if (found == data.objects.end()) io::contractError(io::pointerChild(path, "type"), "Unknown object type");
    const auto propertyPath = io::pointerChild(path, "properties");
    state.accountObject(object.properties, propertyPath, 3);
    state.validate(data.object_contracts[found->second].properties, Value(object.properties), propertyPath);
    const auto& keyName = data.definition.objects[found->second].primary_key;
    ObjectRef ref{object.type, object.properties.at(keyName)};
    (void)objectKey(ref, io::pointerChild(propertyPath, keyName), state.limits.json);
    return ref;
}
bool matches(const CatalogData& data, const TypeSelector& selector, std::string_view object) {
    const auto found = data.objects.find(object);
    if (found == data.objects.end()) return false;
    return selector.kind == SelectorKind::object ? selector.type == object : data.object_contracts[found->second].interfaces.contains(selector.type);
}
} // namespace ii::json::ontology::detail
