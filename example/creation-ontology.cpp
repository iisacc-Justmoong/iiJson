#include <iiJson/Ontology.h>
#include <iostream>

using namespace ii::json;
using namespace ii::json::ontology;

int main() {
    Definition definition;
    definition.id = "iisacc.creation";
    definition.value_types = {{"Id", Schema::string({1, 64}), {}}, {"Title", Schema::string({1, 80}), {}}};
    definition.properties = {{"identity", "Id", {}}, {"label", "Title", {{"semanticId", Value("urn:iisacc:title")}}}};
    definition.interfaces = {{"Named", {{"title", "label"}}, {}, {}}};
    definition.objects = {
        {"Project", "id", {{"id", "identity"}, {"title", "label"}}, {"Named"}},
        {"Asset", "id", {{"id", "identity"}, {"title", "label"}}, {"Named"}}
    };
    definition.links = {{"contains", {"Project"}, {"Named", SelectorKind::interface_type}, {}, {0, 1}, {}, {}}};
    definition.actions = {{"publish", Schema::object({Field::required("id", Schema::string({1, 64}))}),
                            Schema::boolean(), {{"Project"}, {}}, {}}};
    const auto catalog = Catalog::compile(std::move(definition));
    if (!catalog) { std::cerr << catalog.error().path << ": " << catalog.error().message << '\n'; return 1; }

    GraphData graph;
    graph.objects = {{"Project", {{"id", Value("film-1")}, {"title", Value("First film")}}},
                     {"Asset", {{"id", Value("shot-1")}, {"title", Value("Opening shot")}}}};
    graph.links = {{"contains", {"Project", Value("film-1")}, {"Asset", Value("shot-1")}, {}}};
    graph.metadata = {{"source", Value("creation-ontology-example")}};
    const auto snapshot = catalog->snapshot(std::move(graph));
    if (!snapshot) { std::cerr << snapshot.error().path << ": " << snapshot.error().message << '\n'; return 1; }
    const auto assets = snapshot->neighbors({"Project", Value("film-1")}, "contains");
    if (!assets || assets->size() != 1) return 1;
    const auto arguments = Value(Value::Object{{"id", Value("film-1")}});
    if (!catalog->validate_action("publish", arguments)) return 1;

    // These are portable data contracts. The product supplies execution,
    // persistence and authorization using its own replaceable providers.
    const auto manifest = encode(*catalog);
    if (!manifest) return 1;
    const auto restored = decode<Catalog>(*manifest);
    if (!restored) return 1;
    const auto document = snapshot->to_value();
    if (!document || !restored->load_snapshot(*document)) return 1;
    const auto text = stringify(*document);
    if (!text) return 1;
    std::cout << *text << '\n';
    std::cout << "Validated catalog, snapshot, relationship traversal and publish arguments\n";
}
