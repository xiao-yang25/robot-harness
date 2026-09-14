# C++ coding style

Apply these conventions to product code, adapters, examples and tests. Preserve
external API names when overriding or integrating native interfaces. Historical
research fixtures are outside this style's scope.

## Naming

| Entity | Convention | Example |
|---|---|---|
| Files | `snake_case.hpp` / `snake_case.cpp` | `operation_receipt.hpp` |
| Namespaces | `snake_case` | `robot_harness` |
| Classes, structs, enum types and type aliases | `PascalCase` | `OperationReceipt`, `OperationState` |
| Functions and methods | `snake_case` | `submit_operation()`, `observe_completion()` |
| Parameters, local variables and public struct fields | `snake_case` | `operation_id`, `native_outcome` |
| Private data members | `snake_case_` | `current_generation_` |
| Named constants and enum values | `kPascalCase` | `kDefaultCapacity`, `OperationState::kRunning` |
| Boolean queries | `is_`, `has_` or `can_` when they clarify meaning | `is_ready()`, `has_pending_output()` |

Use concrete domain words and verbs. Keep request, acknowledgement, native
completion and settlement distinct in names, as defined in [Design](DESIGN.md).
Avoid ambiguous `success` flags that collapse these facts. Spell abbreviations as
words in types (`OperationId`, `RosAdapter`); avoid encoding variable types in names.
Serialized/native protocol values retain their specified spelling; enum naming
does not change an external protocol. Avoid macros when a typed constant or function
works; necessary macros use a `ROBOT_HARNESS_` prefix and uppercase words.

## Formatting and headers

[.clang-format](../.clang-format) is the C++ layout authority: LLVM-based layout,
two-space indentation, attached braces, a 100-column limit and left-aligned pointer
declarators. This is a project naming convention, not adoption of all LLVM naming
rules. [.editorconfig](../.editorconfig) supplies UTF-8, LF and editor whitespace
defaults; it preserves Markdown trailing spaces.

Use self-contained `.hpp` headers with include guards such as
`ROBOT_HARNESS_OPERATION_RECEIPT_HPP_`. Include a source file's matching header first
when present. Include what a file uses; do not put `using namespace` directives in
headers. Comments explain constraints and reasons, rather than restating code.

## Checking changes

Run the formatter on changed C++ files and check the resulting diff. For the
current M0 sources, from the repository root:

```sh
clang-format --dry-run --Werror src/core.cpp tests/core_smoke.cpp
```

Use `clang-format -i` with explicit file paths to apply formatting; include new
headers and sources as they are added. Do not format build output or historical
fixtures. Naming is checked during review; clang-format checks layout only.
Formatter validation is separate from build and behavior checks in
[Testing](TESTING.md). The formatter is a development tool, not a Core build
dependency; the current Ubuntu workflow does not run a formatting job.

Build trees (`build/`, `build-*/`), compiler output and local CMake caches stay
ignored. Commit source, tests, CMake/CI definitions and shared style configuration.
