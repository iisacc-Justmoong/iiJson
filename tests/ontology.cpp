#define interface iiJson_Interface_Macro_Must_Not_Expand
#include <iiJson/Ontology.h>
#undef interface
#include <iostream>
#include <stdexcept>

using namespace ii::json;
using namespace ii::json::ontology;
namespace {
int checks = 0;
void check(bool ok, const char* text) { ++checks; if (!ok) throw std::runtime_error(text); }
template<class T> void rejected(const Result<T>& result, std::string_view path = {}) {
    check(!result, "Expected ontology rejection");
    if (!path.empty()) check(result.error().path == path, "Ontology error pointer");
}
Definition definition() {
    Definition d;
    d.id = "iisacc.creation";
    d.value_types = {{"Id", Schema::string({1, 64}), {}}, {"Title", Schema::string({1, 80}), {}},
                    {"State", Schema::enumeration({Value("draft"), Value("published")}), {}}};
    d.properties = {{"identity", "Id", {}}, {"label", "Title", {{"semanticId", Value("urn:iisacc:title")}}}, {"state", "State", {}}};
    d.interfaces = {{"Named", {{"title", "label"}}, {}, {}}};
    d.objects = {
        {"Project", "id", {{"id", "identity"}, {"title", "label"}, {"state", "state", Presence::optional}}, {"Named"}},
        {"Asset", "id", {{"id", "identity"}, {"title", "label"}}, {"Named"}}
    };
    d.links = {{"contains", {"Project"}, {"Named", SelectorKind::interface_type}, {0, 2}, {0, 1}, {}, {}}};
    d.actions = {{"publish", Schema::object({Field::required("id", Schema::string({1, 64}))}), Schema::boolean(), {{"Project"}, {}}, {}}};
    d.functions = {{"assetCount", Schema::object({Field::required("id", Schema::string({1, 64}))}),
                    Schema::unsigned_integer(), {{"Project", "Asset"}, {"contains"}}, {}}};
    return d;
}
GraphData graph() {
    return {{{"Project", {{"id", Value("p1")}, {"title", Value("First film")}}},
             {"Asset", {{"id", Value("a1")}, {"title", Value("Shot one")}}},
             {"Asset", {{"id", Value("a2")}, {"title", Value("Shot two")}}}},
            {{"contains", {"Project", Value("p1")}, {"Asset", Value("a1")}, {}}},
            {{"source", Value("native-test")}}};
}
}
int main() {
    auto compiled = Catalog::compile(definition());
    check(compiled.has_value(), "Compile semantic catalog");
    const auto catalog = *compiled;
    check(catalog.implements("Asset", "Named") && catalog.implements("Project", "Named"), "Interface implementation");
    check(!catalog.implements("Missing", "Named"), "Unknown object type");
    auto json = encode(catalog);
    check(json.has_value(), "Export ontology manifest");
    auto imported = decode<Catalog>(*json);
    check(imported.has_value() && encode(*imported).value() == *json, "Ontology stable import/export");
    check(imported->definition().properties[1].metadata.at("semanticId") == Value("urn:iisacc:title"), "Semantic metadata survives import");
    auto detached = definition();
    auto owned = Catalog::compile(detached);
    detached.objects[0].primary_key = "changed";
    check(owned->definition().objects[0].primary_key == "id", "Catalog owns its definition");
    check(catalog.validate_object(graph().objects[0])->id == Value("p1"), "Object primary key");
    rejected(catalog.validate_object(Object{"Missing", {}}), "/type");
    rejected(catalog.validate_object(Object{"Asset", {{"id", Value("a")}, {"title", Value(3)}}}), "/properties/title");
    check(catalog.validate_action("publish", Value(Value::Object{{"id", Value("p1")}})).has_value(), "Action arguments");
    rejected(catalog.validate_action("publish", Value(Value::Object{})), "/arguments/id");
    rejected(catalog.validate_action_result("publish", Value(1)), "/result");
    check(catalog.validate_action_result("publish", Value(true)).has_value(), "Action result");
    rejected(catalog.validate_function_result("assetCount", Value(-1)), "/result");
    check(catalog.validate_function_result("assetCount", Value(2)).has_value(), "Function result");
    rejected(catalog.validate_function("missing", Value()), "/function");

    auto snapshot = catalog.snapshot(graph());
    check(snapshot.has_value(), "Validate ontology snapshot");
    check(snapshot->find({"Asset", Value("a1")})->get().type == "Asset", "Indexed object lookup");
    const auto neighbors = snapshot->neighbors({"Project", Value("p1")}, "contains");
    check(neighbors && neighbors->size() == 1 && neighbors->front().id == Value("a1"), "Typed outgoing traversal");
    check(snapshot->neighbors({"Asset", Value("a1")}, "contains", Direction::incoming)->front().id == Value("p1"), "Incoming traversal");
    check(snapshot->neighbors({"Asset", Value("a2")})->empty(), "Disconnected object");
    rejected(snapshot->find({"Asset", Value("absent")}));
    rejected(snapshot->neighbors({"Project", Value("p1")}, "missing"));
    rejected(snapshot->neighbors({"Project", Value("p1")}, "contains", Direction::outgoing, {0, 100}));
    auto snapshotText = stringify(snapshot->to_value().value()).value();
    auto reloaded = imported->load_snapshot(parse(snapshotText).value());
    check(reloaded && stringify(reloaded->to_value().value()).value() == snapshotText, "Snapshot round trip");
    check(reloaded->data().metadata.at("source") == Value("native-test"), "Snapshot provenance metadata survives import");
    auto mutableGraph = graph();
    auto immutable = catalog.snapshot(mutableGraph);
    mutableGraph.objects[1].properties["title"] = Value("Changed outside snapshot");
    check(immutable->find({"Asset", Value("a1")})->get().properties.at("title") == Value("Shot one"), "Snapshot owns input data");
    auto selfGraph = graph();
    selfGraph.links.push_back({"contains", {"Project", Value("p1")}, {"Project", Value("p1")}, {}});
    auto selfSnapshot = catalog.snapshot(selfGraph);
    check(selfSnapshot && selfSnapshot->neighbors({"Project", Value("p1")}, {}, Direction::both)->size() == 2, "Graph cycles allowed and bidirectional self-links deduplicated");
    auto polymorphic = definition();
    polymorphic.interfaces.push_back({"Media", {}, {"Named"}, {}});
    polymorphic.objects[1].interfaces = {"Media"};
    auto inherited = Catalog::compile(polymorphic);
    check(inherited && inherited->implements("Asset", "Named") && inherited->implements("Asset", "Media"), "Transitive interface implementation");
    check(inherited->snapshot(graph()).has_value(), "Link selector accepts inherited interface");
    auto changedRevision = definition(); changedRevision.revision = 2;
    rejected(Catalog::compile(changedRevision)->load_snapshot(parse(snapshotText).value()), "/ontology/revision");
    auto differentOntology = definition(); differentOntology.id = "another.ontology";
    rejected(Catalog::compile(differentOntology)->load_snapshot(parse(snapshotText).value()), "/ontology/id");
    auto unknownManifest = parse(*json)->get<Value::Object>()->get();
    unknownManifest.emplace("unknown", Value(true));
    rejected(Catalog::from_value(Value(unknownManifest)), "/unknown");
    unknownManifest.erase("unknown"); unknownManifest["formatVersion"] = Value(2);
    rejected(Catalog::from_value(Value(unknownManifest)), "/formatVersion");

    auto duplicate = graph(); duplicate.objects.push_back(duplicate.objects[1]);
    rejected(catalog.snapshot(duplicate), "/objects/3/properties/id");
    auto dangling = graph(); dangling.links[0].target.id = Value("absent");
    rejected(catalog.snapshot(dangling), "/links/0/target");
    auto wrongEndpoint = graph(); wrongEndpoint.links[0].source = {"Asset", Value("a2")};
    rejected(catalog.snapshot(wrongEndpoint), "/links/0/source");
    auto duplicateLink = graph(); duplicateLink.links.push_back(duplicateLink.links[0]);
    rejected(catalog.snapshot(duplicateLink), "/links/1");
    auto targetMultiplicity = graph();
    targetMultiplicity.objects.push_back({"Project", {{"id", Value("p2")}, {"title", Value("Second film")}}});
    targetMultiplicity.links.push_back({"contains", {"Project", Value("p2")}, {"Asset", Value("a1")}, {}});
    rejected(catalog.snapshot(targetMultiplicity), "/links/1/target");
    auto sourceMultiplicity = graph();
    sourceMultiplicity.links.push_back({"contains", {"Project", Value("p1")}, {"Asset", Value("a2")}, {}});
    sourceMultiplicity.links.push_back({"contains", {"Project", Value("p1")}, {"Project", Value("p1")}, {}});
    rejected(catalog.snapshot(sourceMultiplicity), "/links/2/source");
    auto requiredLink = definition(); requiredLink.links[0].targets_per_source.minimum = 1;
    auto disconnected = graph(); disconnected.links.clear();
    rejected(Catalog::compile(requiredLink)->snapshot(disconnected), "/objects/0");

    auto unknown = definition(); unknown.properties[0].value_type = "absent";
    rejected(Catalog::compile(unknown), "/properties/0/valueType");
    auto duplicateType = definition(); duplicateType.objects.push_back(duplicateType.objects[0]);
    rejected(Catalog::compile(duplicateType), "/objects/2/id");
    auto missingProperty = definition(); missingProperty.objects[0].properties.erase(missingProperty.objects[0].properties.begin() + 1);
    rejected(Catalog::compile(missingProperty));
    auto optionalKey = definition(); optionalKey.objects[0].properties[0].presence = Presence::optional;
    rejected(Catalog::compile(optionalKey));
    auto wrongKey = definition(); wrongKey.objects[0].primary_key = "state";
    rejected(Catalog::compile(wrongKey));
    auto cycle = definition(); cycle.interfaces[0].extends = {"Named"};
    rejected(Catalog::compile(cycle));
    auto unknownEffect = definition(); unknownEffect.actions[0].writes.objects.push_back("absent");
    rejected(Catalog::compile(unknownEffect));
    auto wrongSemantic = definition(); wrongSemantic.objects[0].properties[1].property = "identity";
    rejected(Catalog::compile(wrongSemantic));
    auto nullableKey = definition(); nullableKey.value_types[0].schema = Schema::string().nullable();
    rejected(Catalog::compile(nullableKey));
    auto conflict = definition();
    conflict.interfaces.push_back({"DifferentTitle", {{"title", "identity"}}, {}, {}});
    conflict.interfaces.push_back({"Conflicting", {}, {"Named", "DifferentTitle"}, {}});
    rejected(Catalog::compile(conflict));
    auto weak = definition(); weak.objects[0].properties[1].presence = Presence::optional;
    rejected(Catalog::compile(weak));
    auto zeroRevision = definition(); zeroRevision.revision = 0;
    rejected(Catalog::compile(zeroRevision), "/revision");
    auto invalidName = definition(); invalidName.id = "invalid id";
    rejected(Catalog::compile(invalidName), "/id");
    auto binaryMetadata = definition(); binaryMetadata.metadata.emplace("binary", Value(ii::json::Bytes{}));
    const auto binaryError = Catalog::compile(binaryMetadata);
    check(!binaryError && binaryError.error().code == ErrorCode::unsupported_value, "Binary metadata rejected");
    auto invalidKey = graph(); invalidKey.objects[0].properties.emplace(std::string(1, '\xff'), Value(1));
    const auto invalidKeyError = catalog.snapshot(invalidKey);
    check(!invalidKeyError && invalidKeyError.error().code == ErrorCode::invalid_utf8, "Invalid raw property keys use Result errors");
    auto badProperty = graph(); badProperty.links[0].properties.emplace("unexpected", Value(1));
    rejected(catalog.snapshot(badProperty), "/links/0/properties/unexpected");
    auto numeric = definition(); numeric.value_types[0].schema = Schema::unsigned_integer();
    auto numericCatalog = Catalog::compile(numeric);
    auto numericGraph = graph();
    numericGraph.objects[0].properties["id"] = Value(std::uint64_t(1));
    numericGraph.objects[1].properties["id"] = Value(2);
    numericGraph.objects[2].properties["id"] = Value(3);
    numericGraph.links[0].source.id = Value(1); numericGraph.links[0].target.id = Value(std::uint64_t(2));
    auto numericSnapshot = numericCatalog->snapshot(numericGraph);
    check(numericSnapshot && numericSnapshot->find({"Project", Value(1)}).has_value(), "Exact signed/unsigned identity equivalence");
    auto numericDuplicate = numericGraph; numericDuplicate.objects.push_back(numericDuplicate.objects[0]);
    numericDuplicate.objects.back().properties["id"] = Value(1);
    rejected(numericCatalog->snapshot(numericDuplicate));
    auto longIdentityDefinition = definition();
    longIdentityDefinition.value_types[0].schema = Schema::string({1, 2 * 1024 * 1024});
    auto longIdentityCatalog = Catalog::compile(longIdentityDefinition);
    const auto longId = std::string(1024 * 1024 + 1, 'x');
    OntologyLimits longLimits; longLimits.json.string_bytes = 2 * 1024 * 1024;
    GraphData longGraph;
    longGraph.objects.push_back({"Project", {{"id", Value(longId)}, {"title", Value("Large identity")}}});
    auto longSnapshot = longIdentityCatalog->snapshot(std::move(longGraph), longLimits);
    check(longSnapshot && longSnapshot->find({"Project", Value(longId)}).has_value(), "Identity index and lookup honor configured JSON limits");
    auto callback = definition(); callback.value_types[0].schema = Schema::string().refine([](const Value&) -> Result<void> { return {}; });
    const auto callbackError = Catalog::compile(callback);
    check(!callbackError && callbackError.error().code == ErrorCode::unsupported_value, "Portable catalog cannot lose native rules");
    OntologyLimits limits; limits.objects = 1;
    rejected(catalog.snapshot(graph(), limits));
    limits = {}; limits.definitions = 1;
    rejected(Catalog::compile(definition(), limits));
    limits = {}; limits.work = 1;
    const auto budgetError = catalog.snapshot(graph(), limits);
    check(!budgetError && budgetError.error().code == ErrorCode::limit_exceeded, "Whole-graph work budget");
    limits = {}; limits.links = 0;
    rejected(catalog.snapshot(graph(), limits), "/links");
    limits = {}; limits.json.items = 2;
    rejected(catalog.snapshot(graph(), limits));
    rejected(snapshot->neighbors({"Project", Value("p1")}, {}, Direction::both, {100, 0}));
    std::cout << checks << " ontology contract checks passed\n";
}
