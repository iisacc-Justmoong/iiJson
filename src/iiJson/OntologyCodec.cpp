#include <iiJson/Ontology.h>
#include "detail/OntologyData.h"

namespace ii::json::ontology::detail {
namespace {
Value strings(const std::vector<std::string>& values) {
    Value::Array result;
    for (const auto& value : values) result.emplace_back(value);
    return Value(std::move(result));
}
std::vector<std::string> readStrings(const Value& value, std::string_view path, State& state) {
    std::vector<std::string> result;
    for (const auto& item : io::readArray(value, path)) {
        const auto at = io::pointerChild(path, std::to_string(result.size()));
        state.step(at); result.push_back(io::readText(item, at));
    }
    return result;
}
Value metadata(const Value::Object& value, State& state, std::string_view path) {
    state.accountObject(value, path);
    return Value(value);
}
Value schema(const Schema& schema, State& state, std::string_view path) {
    state.step(path);
    auto result = schema.definition(state.limits.json);
    if (!result) { auto error = result.error(); error.path = std::string(path) + error.path; throw error; }
    return std::move(*result);
}
Schema readSchema(const Value& value, State& state, std::string_view path) {
    state.step(path);
    auto result = Schema::from_definition(value, state.limits.json);
    if (!result) { auto error = result.error(); error.path = std::string(path) + error.path; throw error; }
    return std::move(*result);
}
Value bindings(const std::vector<PropertyBinding>& properties, State& state, std::string_view path) {
    Value::Array result;
    for (const auto& property : properties) {
        const auto at = io::pointerChild(path, std::to_string(result.size()));
        state.step(at); state.text(property.name, at); state.text(property.property, at);
        result.emplace_back(Value::Object{{"name", Value(property.name)}, {"property", Value(property.property)},
            {"presence", Value(property.presence == Presence::required ? "required" : "optional")}});
    }
    return Value(std::move(result));
}
std::vector<PropertyBinding> readBindings(const Value& value, std::string_view path, State& state) {
    std::vector<PropertyBinding> result;
    for (const auto& item : io::readArray(value, path)) {
        const auto at = io::pointerChild(path, std::to_string(result.size())); state.step(at);
        io::ObjectReader reader(item, at, {"name", "property", "presence"});
        const auto& presence = io::readText(reader.required("presence"), reader.child("presence"));
        if (presence != "required" && presence != "optional") io::contractError(reader.child("presence"), "Unknown property presence");
        result.push_back({io::readText(reader.required("name"), reader.child("name")),
                          io::readText(reader.required("property"), reader.child("property")),
                          presence == "required" ? Presence::required : Presence::optional});
    }
    return result;
}
Value selector(const TypeSelector& selector) {
    return Value(Value::Object{{"kind", Value(selector.kind == SelectorKind::object ? "object" : "interface")}, {"type", Value(selector.type)}});
}
TypeSelector readSelector(const Value& value, std::string path) {
    io::ObjectReader reader(value, path, {"kind", "type"});
    const auto& kind = io::readText(reader.required("kind"), reader.child("kind"));
    if (kind != "object" && kind != "interface") io::contractError(reader.child("kind"), "Invalid type selector");
    return {io::readText(reader.required("type"), reader.child("type")), kind == "object" ? SelectorKind::object : SelectorKind::interface_type};
}
Value bounds(SizeBounds bounds) {
    Value::Object result{{"minimum", Value(std::uint64_t(bounds.minimum))}};
    if (bounds.maximum != std::numeric_limits<std::size_t>::max()) result.emplace("maximum", Value(std::uint64_t(bounds.maximum)));
    return Value(std::move(result));
}
SizeBounds readBounds(const Value& value, std::string path) {
    io::ObjectReader reader(value, path, {"minimum", "maximum"});
    SizeBounds result;
    if (const auto* low = reader.optional("minimum")) result.minimum = io::readSize(*low, reader.child("minimum"));
    if (const auto* high = reader.optional("maximum")) result.maximum = io::readSize(*high, reader.child("maximum"));
    return result;
}
Value scope(const TypeScope& scope) { return Value(Value::Object{{"objects", strings(scope.objects)}, {"links", strings(scope.links)}}); }
TypeScope readScope(const Value& value, std::string path, State& state) {
    io::ObjectReader reader(value, path, {"objects", "links"});
    return {readStrings(reader.required("objects"), reader.child("objects"), state), readStrings(reader.required("links"), reader.child("links"), state)};
}
Value::Object common(std::string_view id, const Value::Object& meta, State& state, std::string_view path) {
    return {{"id", Value(id)}, {"metadata", metadata(meta, state, io::pointerChild(path, "metadata"))}};
}
Value::Object readMetadata(const io::ObjectReader& reader) {
    const auto* meta = reader.optional("metadata");
    return meta ? io::readObject(*meta, reader.child("metadata")) : Value::Object{};
}
template<class T, class Function> Value list(const std::vector<T>& values, State& state, std::string_view path, Function encode) {
    Value::Array result;
    for (const auto& value : values) {
        const auto at = io::pointerChild(path, std::to_string(result.size())); state.step(at);
        auto item = encode(value, at);
        state.account(item, at, 3);
        result.push_back(std::move(item));
    }
    return Value(std::move(result));
}
template<class T, class Function> std::vector<T> readList(const Value& value, std::string_view path, State& state, Function decode) {
    std::vector<T> result;
    for (const auto& item : io::readArray(value, path)) {
        const auto at = io::pointerChild(path, std::to_string(result.size())); state.step(at);
        result.push_back(decode(item, at));
    }
    return result;
}
Value reference(const ObjectRef& ref) { return Value(Value::Object{{"type", Value(ref.type)}, {"id", ref.id}}); }
ObjectRef readReference(const Value& value, std::string path) {
    io::ObjectReader reader(value, path, {"type", "id"});
    return {io::readText(reader.required("type"), reader.child("type")), reader.required("id")};
}
}

Value encodeDefinition(const Definition& d, State& state) {
    Value::Object result{{"format", Value("iiJson.Ontology")}, {"formatVersion", Value(1)}, {"id", Value(d.id)}, {"revision", Value(d.revision)}};
    result.emplace("metadata", metadata(d.metadata, state, "/metadata"));
    result.emplace("valueTypes", list(d.value_types, state, "/valueTypes", [&](const ValueType& value, const auto& path) {
        auto out = common(value.id, value.metadata, state, path);
        out.emplace("schema", schema(value.schema, state, path + "/schema")); return Value(std::move(out));
    }));
    result.emplace("properties", list(d.properties, state, "/properties", [&](const PropertyType& value, const auto& path) {
        auto out = common(value.id, value.metadata, state, path); out.emplace("valueType", Value(value.value_type)); return Value(std::move(out));
    }));
    result.emplace("interfaces", list(d.interfaces, state, "/interfaces", [&](const InterfaceType& value, const auto& path) {
        auto out = common(value.id, value.metadata, state, path); out.emplace("properties", bindings(value.properties, state, path + "/properties"));
        out.emplace("extends", strings(value.extends)); return Value(std::move(out));
    }));
    result.emplace("objects", list(d.objects, state, "/objects", [&](const ObjectType& value, const auto& path) {
        auto out = common(value.id, value.metadata, state, path);
        out.emplace("primaryKey", Value(value.primary_key)); out.emplace("properties", bindings(value.properties, state, path + "/properties"));
        out.emplace("interfaces", strings(value.interfaces)); out.emplace("additional", Value(value.additional == AdditionalFields::allow ? "allow" : "reject"));
        return Value(std::move(out));
    }));
    result.emplace("links", list(d.links, state, "/links", [&](const LinkType& value, const auto& path) {
        auto out = common(value.id, value.metadata, state, path); out.emplace("source", selector(value.source)); out.emplace("target", selector(value.target));
        out.emplace("targetsPerSource", bounds(value.targets_per_source)); out.emplace("sourcesPerTarget", bounds(value.sources_per_target));
        out.emplace("properties", bindings(value.properties, state, path + "/properties")); return Value(std::move(out));
    }));
    result.emplace("actions", list(d.actions, state, "/actions", [&](const ActionType& value, const auto& path) {
        auto out = common(value.id, value.metadata, state, path); out.emplace("parameters", schema(value.parameters, state, path + "/parameters"));
        out.emplace("result", schema(value.result, state, path + "/result")); out.emplace("writes", scope(value.writes)); return Value(std::move(out));
    }));
    result.emplace("functions", list(d.functions, state, "/functions", [&](const FunctionType& value, const auto& path) {
        auto out = common(value.id, value.metadata, state, path); out.emplace("parameters", schema(value.parameters, state, path + "/parameters"));
        out.emplace("result", schema(value.result, state, path + "/result")); out.emplace("reads", scope(value.reads)); return Value(std::move(out));
    }));
    return Value(std::move(result));
}

Definition decodeDefinition(const Value& value, State& state) {
    io::ObjectReader root(value, "", {"format", "formatVersion", "id", "revision", "metadata", "valueTypes", "properties", "interfaces", "objects", "links", "actions", "functions"});
    if (io::readText(root.required("format"), "/format") != "iiJson.Ontology") io::contractError("/format", "Unknown ontology manifest format");
    if (io::readUnsigned(root.required("formatVersion"), "/formatVersion") != 1) io::contractError("/formatVersion", "Unsupported ontology manifest version");
    Definition d; d.id = io::readText(root.required("id"), "/id"); d.revision = io::readUnsigned(root.required("revision"), "/revision");
    d.metadata = readMetadata(root);
    d.value_types = readList<ValueType>(root.required("valueTypes"), "/valueTypes", state, [&](const Value& entry, const auto& path) {
        io::ObjectReader r(entry, path, {"id", "schema", "metadata"});
        return ValueType{io::readText(r.required("id"), r.child("id")), readSchema(r.required("schema"), state, r.child("schema")), readMetadata(r)};
    });
    d.properties = readList<PropertyType>(root.required("properties"), "/properties", state, [&](const Value& entry, const auto& path) {
        io::ObjectReader r(entry, path, {"id", "valueType", "metadata"});
        return PropertyType{io::readText(r.required("id"), r.child("id")), io::readText(r.required("valueType"), r.child("valueType")), readMetadata(r)};
    });
    d.interfaces = readList<InterfaceType>(root.required("interfaces"), "/interfaces", state, [&](const Value& entry, const auto& path) {
        io::ObjectReader r(entry, path, {"id", "properties", "extends", "metadata"});
        return InterfaceType{io::readText(r.required("id"), r.child("id")), readBindings(r.required("properties"), r.child("properties"), state),
            readStrings(r.required("extends"), r.child("extends"), state), readMetadata(r)};
    });
    d.objects = readList<ObjectType>(root.required("objects"), "/objects", state, [&](const Value& entry, const auto& path) {
        io::ObjectReader r(entry, path, {"id", "primaryKey", "properties", "interfaces", "additional", "metadata"});
        const auto& additional = io::readText(r.required("additional"), r.child("additional"));
        if (additional != "allow" && additional != "reject") io::contractError(r.child("additional"), "Invalid additional field policy");
        return ObjectType{io::readText(r.required("id"), r.child("id")), io::readText(r.required("primaryKey"), r.child("primaryKey")),
            readBindings(r.required("properties"), r.child("properties"), state), readStrings(r.required("interfaces"), r.child("interfaces"), state),
            additional == "allow" ? AdditionalFields::allow : AdditionalFields::reject, readMetadata(r)};
    });
    d.links = readList<LinkType>(root.required("links"), "/links", state, [&](const Value& entry, const auto& path) {
        io::ObjectReader r(entry, path, {"id", "source", "target", "targetsPerSource", "sourcesPerTarget", "properties", "metadata"});
        return LinkType{io::readText(r.required("id"), r.child("id")), readSelector(r.required("source"), r.child("source")), readSelector(r.required("target"), r.child("target")),
            readBounds(r.required("targetsPerSource"), r.child("targetsPerSource")), readBounds(r.required("sourcesPerTarget"), r.child("sourcesPerTarget")),
            readBindings(r.required("properties"), r.child("properties"), state), readMetadata(r)};
    });
    d.actions = readList<ActionType>(root.required("actions"), "/actions", state, [&](const Value& entry, const auto& path) {
        io::ObjectReader r(entry, path, {"id", "parameters", "result", "writes", "metadata"});
        return ActionType{io::readText(r.required("id"), r.child("id")), readSchema(r.required("parameters"), state, r.child("parameters")),
            readSchema(r.required("result"), state, r.child("result")), readScope(r.required("writes"), r.child("writes"), state), readMetadata(r)};
    });
    d.functions = readList<FunctionType>(root.required("functions"), "/functions", state, [&](const Value& entry, const auto& path) {
        io::ObjectReader r(entry, path, {"id", "parameters", "result", "reads", "metadata"});
        return FunctionType{io::readText(r.required("id"), r.child("id")), readSchema(r.required("parameters"), state, r.child("parameters")),
            readSchema(r.required("result"), state, r.child("result")), readScope(r.required("reads"), r.child("reads"), state), readMetadata(r)};
    });
    return d;
}

Value encodeGraph(const CatalogData& catalog, const GraphData& graph, State& state) {
    Value::Array objects, links;
    for (std::size_t i = 0; i < graph.objects.size(); ++i) {
        const auto path = "/objects/" + std::to_string(i); state.step(path);
        const auto& object = graph.objects[i];
        state.accountObject(object.properties, path + "/properties", 3);
        objects.emplace_back(Value::Object{{"type", Value(object.type)}, {"properties", Value(object.properties)}});
    }
    for (std::size_t i = 0; i < graph.links.size(); ++i) {
        const auto path = "/links/" + std::to_string(i); state.step(path);
        const auto& link = graph.links[i];
        state.accountObject(link.properties, path + "/properties", 3);
        state.account(link.source.id, path + "/source/id", 4); state.account(link.target.id, path + "/target/id", 4);
        links.emplace_back(Value::Object{{"type", Value(link.type)}, {"source", reference(link.source)}, {"target", reference(link.target)}, {"properties", Value(link.properties)}});
    }
    return Value(Value::Object{{"format", Value("iiJson.OntologySnapshot")}, {"formatVersion", Value(1)},
        {"ontology", Value(Value::Object{{"id", Value(catalog.definition.id)}, {"revision", Value(catalog.definition.revision)}})},
        {"objects", Value(std::move(objects))}, {"links", Value(std::move(links))}, {"metadata", metadata(graph.metadata, state, "/metadata")}});
}
GraphData decodeGraph(const CatalogData& catalog, const Value& value, State& state) {
    io::ObjectReader root(value, "", {"format", "formatVersion", "ontology", "objects", "links", "metadata"});
    if (io::readText(root.required("format"), "/format") != "iiJson.OntologySnapshot") io::contractError("/format", "Unknown snapshot format");
    if (io::readUnsigned(root.required("formatVersion"), "/formatVersion") != 1) io::contractError("/formatVersion", "Unsupported snapshot version");
    io::ObjectReader ontology(root.required("ontology"), "/ontology", {"id", "revision"});
    if (io::readText(ontology.required("id"), "/ontology/id") != catalog.definition.id) io::contractError("/ontology/id", "Snapshot belongs to another ontology");
    if (io::readUnsigned(ontology.required("revision"), "/ontology/revision") != catalog.definition.revision) io::contractError("/ontology/revision", "Snapshot ontology revision does not match");
    GraphData graph;
    graph.objects = readList<Object>(root.required("objects"), "/objects", state, [&](const Value& value, const auto& path) {
        io::ObjectReader r(value, path, {"type", "properties"});
        return Object{io::readText(r.required("type"), r.child("type")), io::readObject(r.required("properties"), r.child("properties"))};
    });
    graph.links = readList<Link>(root.required("links"), "/links", state, [&](const Value& value, const auto& path) {
        io::ObjectReader r(value, path, {"type", "source", "target", "properties"});
        return Link{io::readText(r.required("type"), r.child("type")), readReference(r.required("source"), r.child("source")),
            readReference(r.required("target"), r.child("target")), io::readObject(r.required("properties"), r.child("properties"))};
    });
    graph.metadata = readMetadata(root);
    return graph;
}
} // namespace ii::json::ontology::detail
