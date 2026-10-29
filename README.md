# iiJson

[Windows build and execution](docs/WindowsBuildAndRun.md)

Package version: **0.1.2**. This patch adds an explicit last-key-wins parsing
policy for compatible legacy formats while retaining strict parsing by default.
Earlier versioned feature notes remain historical.


iiJson provides native C++23 JSON, domain schemas and optional ontology
contracts independently of other SDK modules. The JSON value tree, error
contract and UTF-8 validation belong to this module. It imports no Core,
runtime, thread pool, file provider or Qt package and does not create files.

The contract tests include `<optional>` directly when defining their optional
settings field, so MSVC builds do not depend on libc++ transitive includes.

```cmake
find_package(iiJson 0.1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE iiJson::iiJson)
```

```cpp
#include <iiJson/Json.h>
auto tree = ii::json::parse(R"({"schemaVersion":1,"title":null})");
if (!tree) { /* inspect code, byte offset, line, column and message */ }
auto bytes = ii::json::stringify(*tree);
```

The codec implements an RFC 8259 profile: UTF-8 only, no BOM, comments or trailing
commas, no duplicate decoded keys, no unpaired surrogates, no nonfinite numbers.
Escaped NUL is preserved. Missing keys, explicit null, false, empty strings and
empty containers remain distinct. Integers require exact signed/unsigned 64-bit
range; nonnegative values up to INT64_MAX decode as signed integers, larger ones
as unsigned. Integer overflow never falls back to double. Decimal/exponent tokens
decode to finite binary64; underflow and overflow are rejected. Binary64 rounding
otherwise follows the target standard library. Targets without floating
`from_chars` use a private classic-locale stream, with the same overflow,
underflow-to-zero and representable-subnormal tests; no process locale changes.
Output uses a locale-independent classic stream, retains
a decimal/exponent marker for real values (including negative zero), UTF-8 byte
key order and compact escapes. JSON cannot retain small positive integers' C++
signed/unsigned distinction. Binary Value alternatives require a domain encoding.

`Limits` bounds input bytes, output bytes, decoded bytes per string, total items
(each value and object key), and depth. The root has depth one, including scalars;
the hard ceiling is 256. Defaults are 8 MiB input/output, 1 MiB per string,
100,000 items, depth 64. No partial document/output is returned on failure. Bad
allocation is an exception; grammar/policy errors are `expected` errors. Parsing
does not normalize Unicode, interpret paths/URLs, infer dates or coerce DTOs.

Formats with an existing last-key-wins contract may explicitly call
`parse_with_duplicate_keys(text, DuplicateKeyPolicy::last_wins, limits)`.
Normal `parse`, schema parsing and `Codec<T>` remain strict. Every repeated
value still consumes the normal grammar and resource limits; validate the
returned value separately when the legacy format also needs a schema. See the
[duplicate-key policy](docs/duplicate-key-policy.md) and
[example](example/legacy-duplicate-policy.cpp).

Specialize `ii::json::Codec<T>` with explicit `encode(const T&) -> Result<Value>`
and `decode(const Value&) -> Result<T>`. `encode<T>` / `decode<T>` apply the
bounded text codec around these conversions. Existing manual codecs remain
supported; see `tests/contracts.cpp`.

## Domain schemas

Include `<iiJson/Schema.h>` to declare owned, immutable domain schemas. Compose
required/optional fields, nested objects and arrays, nullable values, numeric
ranges, Unicode string lengths, literals, enumerations, exactly-one alternatives
and custom business rules. Unknown object fields are rejected by default.

```cpp
#include <iiJson/Schema.h>
using namespace ii::json;
const auto schema = Schema::object({
    Field::required("schemaVersion", Schema::literal(Value(1))),
    Field::required("title", Schema::string({1, 80})),
    Field::optional("comment", Schema::string().nullable())
});
auto project = parse(R"({"schemaVersion":1,"title":"First film"})", schema);
if (project) {
    auto output = stringify(*project, schema);
}
```

Validation returns `Result<void>` and reports an RFC 6901 pointer in
`Error::path`, such as `/tracks/0/name`. It neither inserts missing values nor
coerces types. Document resource limits also apply to unrestricted/unknown
fields; `Limits::schema_steps` independently bounds schema evaluation.
Adding `static const Schema& schema()` or `static Schema schema()` to a `Codec<T>`
automatically validates decoded trees before DTO conversion and encoded trees
before JSON output. The runnable [domain example](example/domain-schema.cpp)
shows the complete integration. See the Korean
[domain schema contract](docs/domain-schema.md) for exact semantics and limits.

These are native C++ domain contracts; loading external JSON Schema documents
or resolving remote references is outside this API. Qt, storage and Core Runtime
dependencies remain absent. Rebuild consumers together with the library because
the public error and limits structures gained fields.

Schema definitions can also be exported with `schema.definition()` and restored
with `Schema::from_definition(value)`, or encoded using `Codec<Schema>`.
The versioned format preserves validation semantics and rejects native
refinements instead of silently discarding them. `SchemaBudget` shares an
evaluation budget across multiple validations.

## Ontology contracts

The separate `iiJson::Ontology` target provides a portable semantic contract layer
inspired by the object, property, link, interface and operation concepts of the
[Palantir Ontology](https://www.palantir.com/docs/foundry/ontology/overview).
It adds reusable value types and semantic properties, polymorphic object types,
typed links with cardinality constraints, and action/function input, output and
capability declarations. Compiled catalogs and validated graph snapshots are
owned and immutable. Snapshots support indexed object lookup and bounded,
directional relationship traversal.

```cmake
find_package(iiJson 0.1 CONFIG REQUIRED COMPONENTS Ontology)
target_link_libraries(app PRIVATE iiJson::Ontology)
```

Include `<iiJson/Ontology.h>` and use `ii::json::ontology::Catalog::compile` to
validate a `Definition`. `encode(catalog)` / `decode<Catalog>(text)` exchange
versioned manifests. `catalog.snapshot(data)` validates object identities,
interfaces, link endpoints, duplicate edges and both cardinality bounds before
returning a snapshot. Snapshot documents bind to an ontology ID and revision.

The [creation example](example/creation-ontology.cpp) demonstrates a Project/Asset
model, shared properties, an interface-based relationship, traversal, portable
round trips and publish-action argument validation. The Korean
[ontology contract](docs/ontology.md) documents the format, limits and extension
boundaries. Execution, storage, authorization and inference belong to consumer
modules; read/write declarations are descriptive contracts, not permissions.

The dependency direction within this module is Ontology → iiJson. Existing codec
users keep linking `iiJson::iiJson` without an ontology dependency. Neither target
imports Core, Qt, storage, network or thread-pool packages.

This output is not declared byte-compatible with Qt JSON or an RFC 8785 canonical
encoding. Do not change Sync's hash input to this writer without format/version
migration. The first consumer, iiCSMIDI, uses it to validate an embedded metadata
boundary while the existing Native authorship codec retains stored schema and
format semantics.

Build, test and stage under `build/`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_iiSDKCore=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/install"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build
```

Alternatively, run `./install.sh` to build, test, stage under `build/install`,
and run the independent installed consumer.

The independent consumer in `tests/consumer` disables Qt, Threads and provider
and Core discovery and checks that no Core target was imported. Native and WASM
build/execution evidence are recorded by the integration validation, separately.

Original code is MIT; see `LICENSE`. Grammar reference:
[RFC 8259](https://www.rfc-editor.org/rfc/rfc8259).

## License

SPDX-License-Identifier: MIT

The original code and documentation in this repository are licensed under the
[MIT License](LICENSE). Third-party code, libraries, fonts, tools, and model
weights retain their respective licenses.

## Source header layout

Public headers and implementations live together in `src/iiJson/`.
There is no separate authored `include/` tree. Existing public `#include` names
and exported CMake target names remain unchanged; installation still places only public
headers in the package include directory. Generated export headers belong to
`build/`, not to the authored source tree.

## Value migration

`ii::json::Value` is now a distinct type, not an alias of `ii::core::Value`.
`ValueKind`, `Bytes` and value-access errors now belong to `ii::json`. Callers
that shared Core values must migrate and rebuild because this changes source
and binary compatibility. Applications can explicitly deep-copy between the
types with the independently configured [Core value adapter](example/CoreValue/);
this adapter is never included or linked by the base SDK. See
[the independence contract](docs/sdk-independence.md).

Validate this layout from the SDK workspace with
`python3 -B tests/test_source_header_layout.py`. Reconfigure, build, run CTest,
and verify a staged installed consumer after changing source/header paths.

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
