# entity

[![CMake](https://github.com/mseminatore/entity/actions/workflows/cmake.yml/badge.svg)](https://github.com/mseminatore/entity/actions/workflows/cmake.yml)

A small, header-only, archetype-based Entity Component System (ECS) for C++23.

Entities are cheap, generation-checked handles. Components are plain structs with no base
class. Entities are grouped into archetypes by their exact set of component types, so 
components of the same type are stored contiguously in memory and iteration over a 
query/view only touches matching archetypes.

## Features

- Header-only: `entity.h`
- Type-erased, archetype-based storage — no inheritance or virtual dispatch on components
- Automatic archetype interning: the same final component set always maps to the same
  archetype, regardless of the order components were added in
- Cached archetype transitions for repeated `add`/`remove` calls
- Generation-checked entity handles — a stale handle safely fails `isAlive()` even after
  its index is recycled
- Simple query API (`view<Components...>()`) supporting both
  `func(Components&...)` and `func(Entity, Components&...)` callback shapes, plus
  `.exclude<Excluded...>()` filtering
- Safe to `add`/`remove`/`destroy` entities from inside a `for_each` callback — an entity
  destroyed or migrated out mid-iteration by an earlier callback is skipped safely rather
  than read after it's invalid
- Precondition contracts (e.g. `add<T>` on an already-present component, an operation on a
  dead entity) are enforced unconditionally via `ENTITY_ASSERT`, independent of `NDEBUG` —
  a Release build still traps a violation with a clean abort instead of undefined behavior

## Minimum Requirements

- A C++23 compiler
- CMake 3.20+

## Getting started

This repository uses the [`testy`](https://github.com/mseminatore/testy) test framework as
a git submodule, so clone with `--recursive` (or init it afterward):

```sh
git clone --recursive <this-repo-url>
# or, if already cloned:
git submodule update --init --recursive
```

Build:

```sh
cmake -S . -B build
cmake --build build
```

This produces the `entity` demo executable, the test binaries under `build/tests`, and the
benchmark binary under `build/benchmarks`.

## Basic Usage

```cpp
#include "entity.h"

// define some Components
struct Position { float x, y; };
struct Velocity { float vx, vy; };

// instantiate the entity manager
EntityManager entityManager;

// create an entity and attach components
Entity e = entityManager.create();
entityManager.add<Position>(e, Position{ 0.0f, 0.0f });
entityManager.add<Velocity>(e, Velocity{ 1.0f, 0.0f });

// iterate every entity that has both components
entityManager.view<Position, Velocity>().for_each([](Position& p, Velocity& v) {
    p.x += v.vx;
    p.y += v.vy;
});

// ... or exclude entities that also have a given component
entityManager.view<Position>().exclude<Velocity>().for_each([](Position& p) {
    // only entities with Position and no Velocity
});

// query for a single entity's component
if (auto pos = entityManager.get<Position>(e)) {
    printf("x=%f y=%f\n", pos->get().x, pos->get().y);
}

entityManager.remove<Velocity>(e);
entityManager.destroy(e);
```

See `main.cpp` for a more complete example (a small game loop with movement and collision systems).

## Unit Testing

Tests are run using CTest, for local and CI testing, and are split into two targets:

- `entity_tests` — fast, correctness suite
- `entity_stress_tests` — larger-scale/randomized tests, tagged with the CTest label `stress`

```sh
cd build
ctest --output-on-failure       # run everything
ctest -LE stress                # fast suite only
ctest -L stress                 # stress suite only
```

Each test binary can be run directly for detailed, syntax-colored, test-case output:

```sh
./build/tests/entity_tests
./build/tests/entity_stress_tests
```

## Benchmarks

A micro benchmark suite lives under `benchmarks/`, covering entity create/destroy,
component add/remove migration chains, random component access, and `view().for_each()`
iteration (including a fragmented-across-many-archetypes case and a collision-shaped
nested-view case). It's a manually-run dev tool for measuring the cost of local changes to
`entity.h`/`archetype.h` — it isn't registered with CTest, since timing isn't a pass/fail
criteria.

The `entity_bench` target always builds with `-O2`/`/O2` regardless of the top-level
configure (no `CMAKE_BUILD_TYPE` is set by default, so an unoptimized build would give
meaningless numbers):

```sh
./build/benchmarks/entity_bench
```
