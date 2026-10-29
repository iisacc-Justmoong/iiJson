#include <iiJson/Ontology.h>
#include "detail/OntologyData.h"
#include <tuple>

namespace ii::json::ontology {
namespace {
namespace io = ii::json::detail;
using detail::State;
using detail::ObjectKey;
using Counts = std::map<ObjectKey, std::size_t>;
std::size_t count(const Counts& counts, const ObjectKey& key) {
    const auto found = counts.find(key);
    return found == counts.end() ? 0 : found->second;
}
}
Snapshot::Snapshot(std::shared_ptr<const detail::SnapshotData> data) : data_(std::move(data)) {}
const GraphData& Snapshot::data() const & noexcept { return data_->graph; }
Result<Snapshot> Catalog::snapshot(GraphData graph, OntologyLimits limits) const {
    try {
        if (graph.objects.size() > limits.objects) io::contractError("/objects", "Object count limit exceeded", ErrorCode::limit_exceeded);
        if (graph.links.size() > limits.links) io::contractError("/links", "Link count limit exceeded", ErrorCode::limit_exceeded);
        State state(limits);
        auto snapshot = std::make_shared<detail::SnapshotData>();
        snapshot->catalog = data_;
        snapshot->identity_limits = limits.json;
        snapshot->graph = std::move(graph);
        auto& result = snapshot->graph;
        state.accountObject(result.metadata, "/metadata");
        for (std::size_t i = 0; i < result.objects.size(); ++i) {
            const auto path = "/objects/" + std::to_string(i);
            auto reference = detail::validateObject(*data_, result.objects[i], state, path);
            auto key = detail::objectKey(reference, path, limits.json);
            if (!snapshot->index.emplace(std::move(key), i).second) {
                const auto& type = data_->definition.objects[data_->objects.at(reference.type)];
                io::contractError(io::pointerChild(path + "/properties", type.primary_key), "Duplicate object identity");
            }
            snapshot->references.push_back(std::move(reference));
        }
        std::set<std::tuple<std::string, ObjectKey, ObjectKey>> edges;
        std::vector<Counts> sourceCounts(data_->definition.links.size()), targetCounts(data_->definition.links.size());
        for (std::size_t i = 0; i < result.links.size(); ++i) {
            const auto path = "/links/" + std::to_string(i); state.step(path);
            const auto& link = result.links[i];
            const auto found = data_->links.find(link.type);
            if (found == data_->links.end()) io::contractError(path + "/type", "Unknown link type");
            const auto index = found->second;
            const auto& type = data_->definition.links[index];
            state.text(link.source.type, path + "/source/type"); state.text(link.target.type, path + "/target/type");
            state.account(link.source.id, path + "/source/id", 4); state.account(link.target.id, path + "/target/id", 4);
            const auto source = detail::objectKey(link.source, path + "/source", limits.json);
            const auto target = detail::objectKey(link.target, path + "/target", limits.json);
            if (!snapshot->index.contains(source)) io::contractError(path + "/source", "Link source is not present in the snapshot");
            if (!snapshot->index.contains(target)) io::contractError(path + "/target", "Link target is not present in the snapshot");
            if (!detail::matches(*data_, type.source, link.source.type)) io::contractError(path + "/source", "Link source type does not match its contract");
            if (!detail::matches(*data_, type.target, link.target.type)) io::contractError(path + "/target", "Link target type does not match its contract");
            state.accountObject(link.properties, path + "/properties", 3);
            state.validate(data_->link_contracts[index], Value(link.properties), path + "/properties");
            if (!edges.emplace(link.type, source, target).second) io::contractError(path, "Duplicate typed link");
            if (++sourceCounts[index][source] > type.targets_per_source.maximum) io::contractError(path + "/source", "Link targets-per-source maximum exceeded");
            if (++targetCounts[index][target] > type.sources_per_target.maximum) io::contractError(path + "/target", "Link sources-per-target maximum exceeded");
            snapshot->outgoing[source].push_back(i); snapshot->incoming[target].push_back(i);
        }
        for (std::size_t i = 0; i < result.objects.size(); ++i) {
            const auto path = "/objects/" + std::to_string(i);
            const auto& reference = snapshot->references[i];
            const auto key = detail::objectKey(reference, path, limits.json);
            for (std::size_t j = 0; j < data_->definition.links.size(); ++j) {
                state.step(path);
                const auto& type = data_->definition.links[j];
                if (detail::matches(*data_, type.source, reference.type) && count(sourceCounts[j], key) < type.targets_per_source.minimum)
                    io::contractError(path, "Link targets-per-source minimum is not satisfied: " + type.id);
                if (detail::matches(*data_, type.target, reference.type) && count(targetCounts[j], key) < type.sources_per_target.minimum)
                    io::contractError(path, "Link sources-per-target minimum is not satisfied: " + type.id);
            }
        }
        // Each representation has its own document bounds; all passes share the
        // operation budget. No partially validated snapshot escapes on failure.
        state.items = 0; state.bytes = 0;
        snapshot->manifest = detail::encodeGraph(*data_, result, state);
        state.validate(Schema::any(), snapshot->manifest, "");
        if (const auto bounded = stringify(snapshot->manifest, limits.json); !bounded) return std::unexpected(bounded.error());
        return Snapshot(std::move(snapshot));
    } catch (const Error& error) { return std::unexpected(error); }
}
Result<Snapshot> Catalog::load_snapshot(const Value& document, OntologyLimits limits) const {
    try {
        State state(limits);
        state.validate(Schema::any(), document, "");
        auto bounds = limits.json; bounds.output_bytes = bounds.input_bytes;
        if (const auto bounded = stringify(document, bounds); !bounded) return std::unexpected(bounded.error());
        auto graph = detail::decodeGraph(*data_, document, state);
        limits.work = state.budget.remaining_steps;
        return snapshot(std::move(graph), limits);
    } catch (const Error& error) { return std::unexpected(error); }
}
Result<Value> Snapshot::to_value(Limits limits) const {
    if (const auto valid = Schema::any().validate(data_->manifest, limits); !valid) return std::unexpected(valid.error());
    if (const auto bounded = stringify(data_->manifest, limits); !bounded) return std::unexpected(bounded.error());
    return data_->manifest;
}
Result<std::reference_wrapper<const Object>> Snapshot::find(const ObjectRef& reference) const & {
    try {
        if (!data_->catalog->objects.contains(reference.type)) io::contractError("/object/type", "Unknown object type");
        const auto found = data_->index.find(detail::objectKey(reference, "/object", data_->identity_limits));
        if (found == data_->index.end()) io::contractError("/object", "Object is not present in the snapshot");
        return std::cref(data_->graph.objects[found->second]);
    } catch (const Error& error) { return std::unexpected(error); }
}
Result<std::vector<ObjectRef>> Snapshot::neighbors(const ObjectRef& reference, std::string_view link_type, Direction direction, QueryLimits limits) const {
    try {
        if (direction != Direction::outgoing && direction != Direction::incoming && direction != Direction::both) io::contractError("/direction", "Invalid traversal direction");
        if (!link_type.empty() && !data_->catalog->links.contains(link_type)) io::contractError("/linkType", "Unknown link type");
        if (const auto object = find(reference); !object) return std::unexpected(object.error());
        SchemaBudget work{limits.steps};
        io::consume(work, "/object");
        const auto key = detail::objectKey(reference, "/object", data_->identity_limits);
        std::map<ObjectKey, ObjectRef> neighbors;
        const auto traverse = [&](const auto& adjacency, bool outgoing) {
            const auto found = adjacency.find(key);
            if (found == adjacency.end()) return;
            for (const auto index : found->second) {
                io::consume(work, "/links");
                const auto& link = data_->graph.links[index];
                if (!link_type.empty() && link.type != link_type) continue;
                const auto& other = outgoing ? link.target : link.source;
                const auto otherKey = detail::objectKey(other, "/object", data_->identity_limits);
                if (!neighbors.contains(otherKey)) {
                    if (neighbors.size() >= limits.results) io::contractError("/results", "Traversal result limit exceeded", ErrorCode::limit_exceeded);
                    neighbors.emplace(otherKey, data_->references[data_->index.at(otherKey)]);
                }
            }
        };
        if (direction != Direction::incoming) traverse(data_->outgoing, true);
        if (direction != Direction::outgoing) traverse(data_->incoming, false);
        std::vector<ObjectRef> result;
        for (auto& [key, reference] : neighbors) { (void)key; result.push_back(std::move(reference)); }
        return result;
    } catch (const Error& error) { return std::unexpected(error); }
}
} // namespace ii::json::ontology
