# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A from-scratch, header-only archetype-based Entity Component System (ECS) in C++20. The library is `entity.h` + `archetype.h` + `component_props.h`. `components.h` (example component structs) and `main.cpp` (a demo game loop) are a *consumer* of the library, not part of it.

## Build & test

```
cmake -S . -B build
cmake --build build
```

Run tests:
```
cd build && ctest --output-on-failure       # everything
ctest -LE stress --output-on-failure        # fast suite only
ctest -L stress --output-on-failure         # slow/stress suite only
```

Run a test binary directly for colored per-case output (not just pass/fail):
```
./build/tests/entity_tests
./build/tests/entity_stress_tests
```

`testy` (the test framework) is a git submodule — if missing after a fresh clone: `git submodule update --init --recursive`.

No `CMAKE_BUILD_TYPE` is set, so builds are unoptimized by default. Library preconditions (see below) are enforced via the `ENTITY_ASSERT` macro (`entity.h`), not bare `assert()` — it checks unconditionally regardless of `NDEBUG`/build type, specifically so a Release build (CI builds both Debug and Release) doesn't silently strip the checks and turn a precondition violation into undefined behavior.

`entity`, `entity_tests`, and `entity_stress_tests` (but not the `testy` submodule) build with `-Wall -Wextra -Werror` (`/W4 /WX` on MSVC) — any new warning fails the build. CI (`.github/workflows/cmake.yml`) builds Debug and Release on both Ubuntu and macOS.

`./build/entity` runs the demo game loop (`main.cpp`); it reads stdin one char per frame and quits on `q`.

## Architecture

**Entity handle.** `Entity` is a packed `uint64_t`: high 32 bits = generation, low 32 bits = index (`entityIndex()` / `entityGeneration()` in `entity.h`). `EntityTable::destroy()` doesn't shrink storage — it frees the index onto a reuse list and bumps that slot's generation, so a stale handle captured before `destroy()` fails `isAlive()`'s generation check even after the index is recycled by a later `create()`.

**Archetype storage.** Every unique *sorted* set of component types is one `Archetype` (`archetype.h`), holding one `Column` per component type — contiguous, type-erased storage (raw `std::byte*` buffer, placement-new / `operator delete` with alignment) driven by function-pointer `ComponentOps` (`component_props.h`: move-construct + destroy + size + alignment), so a `Column` never needs to know its component type at compile time. This is what lets a component like `Name` (holding a `std::string`) migrate between archetypes correctly.

`ArchetypeRegistry` interns archetypes by canonical `Signature` (sorted `vector<ComponentId>`), so the same final component set always resolves to the same `Archetype` regardless of the order components were added in. Each `Archetype` also caches its `addEdge`/`removeEdge` transitions to sibling archetypes for O(1) repeated `add<T>`/`remove<T>` calls.

**`add<T>`/`remove<T>` = full archetype migration**: move-construct every existing component from the old archetype's row into the new archetype's row, placement-new the added component (or skip it for a removal), swap-remove the now-vacated row (`Archetype::finishRemovingRow`), and patch `EntityTable`'s row index for whichever entity got swapped into that slot.

**Precondition contracts, not graceful failures.** `add<T>` asserts (via `ENTITY_ASSERT`) the component is *not* already present; `remove<T>` asserts it *is* present; both assert the entity is alive. `get<T>` / `has<T>` are the graceful counterparts: `get<T>` returns `std::optional<std::reference_wrapper<T>>` (empty if absent) instead of asserting on a missing component, and `has<T>` never asserts on absence. Don't add error handling around the assert-guarded cases — they're deliberate contract violations, and there's no death-test harness in this repo for exercising them. `ENTITY_ASSERT` is intentionally always-on rather than tied to `NDEBUG`: a stripped check wouldn't make the violation "not happen," it would just turn a clean abort into silent memory corruption in a Release build.

**Queries.** `EntityManager::view<Components...>()` matches any archetype that *contains* all requested component ids — extra, unrelated components on a matching entity don't exclude it (it's not an exact-signature match). `EntityView::for_each` accepts either `func(Components&...)` or `func(Entity, Components&...)`, disambiguated via SFINAE on both `invoke` overloads — a lambda with a mismatched arity is a compile error, not a silent misdispatch. `view<>()` with zero template args is a valid (if non-obvious/undocumented) way to visit every live entity across every archetype.

**No thread safety** anywhere (shared vectors in `EntityTable` / `ArchetypeRegistry`, function-local-static component IDs in `ComponentType`) — single-threaded use only, matching `main.cpp`'s single-threaded game loop.

**Header include order matters.** `entity.h` is the only intended entry point: it defines `Entity`/`NullEntity` first, then includes `component_props.h` then `archetype.h`, both of which assume those symbols already exist. Don't include `archetype.h` or `component_props.h` directly.

## Test suite structure

Two CTest targets, both using the `testy` framework (each test file implements `test_main(argc, argv)`; `testy` supplies `main()` and calls into it):

- `tests/entity_tests.cpp` — fast, everyday correctness suite. One `static bool test_xxx()` per behavior, registered via `TESTEX("description", test_xxx())` grouped under `SUITE(...)`.
- `tests/entity_stress_tests.cpp` — slower, larger-scale suite (randomized churn cross-checked against a hand-rolled shadow model, large-N column growth, archetype-signature explosion, big nested-iteration), registered as its own CTest test carrying `LABELS "stress"` so it can be excluded from fast iteration via `ctest -LE stress`.

New tests follow the same pattern: add a `static bool test_xxx()` and one `TESTEX(...)` line in the appropriate file's `test_main()`.
