# iiJson

[Windows 빌드 및 실행](docs/WindowsBuildAndRun.md)

Package release: **0.1.1** (2026-10-19). This patch packages the current
source snapshot; build, test and installation evidence is recorded in the SDK
patch-release report. Earlier versioned feature notes remain historical.


iiJson 0.1.1 is a native C++23 JSON codec above `iiSDKCore::Contracts` 0.14.
Core remains independent of codecs, storage, networking and Qt. This package
imports no runtime, thread pool or file provider. It does not create files.

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
Output uses locale-independent `to_chars`, retains
a decimal/exponent marker for real values (including negative zero), UTF-8 byte
key order and compact escapes. JSON cannot retain small positive integers' C++
signed/unsigned distinction. Binary Value alternatives require a domain encoding.

`Limits` bounds input bytes, output bytes, decoded bytes per string, total items
(each value and object key), and depth. The root has depth one, including scalars;
the hard ceiling is 256. Defaults are 8 MiB input/output, 1 MiB per string,
100,000 items, depth 64. No partial document/output is returned on failure. Bad
allocation is an exception; grammar/policy errors are `expected` errors. Parsing
does not normalize Unicode, interpret paths/URLs, infer dates or coerce DTOs.

Specialize `ii::json::Codec<T>` with explicit `encode(const T&) -> Result<Value>`
and `decode(const Value&) -> Result<T>`. Domain schemas own field omission,
version checks and numeric conversions. `encode<T>` / `decode<T>` apply the
bounded text codec around these conversions. See `tests/contracts.cpp`.

This output is not declared byte-compatible with Qt JSON or an RFC 8785 canonical
encoding. Do not change Sync's hash input to this writer without format/version
migration. The first consumer, iiCSMIDI, uses it to validate an embedded metadata
boundary while the existing Native authorship codec retains stored schema and
format semantics.

Build, test and stage under `build/`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/iiSDKCore -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/install"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build
```

The independent consumer in `tests/consumer` disables Qt, Threads and provider
discovery and checks that no Core runtime target was imported. Native and WASM
build/execution evidence are recorded by the integration validation, separately.

Original code is AGPL-3.0-only; see `LICENSE`. Grammar reference:
[RFC 8259](https://www.rfc-editor.org/rfc/rfc8259).
