# entity

[![CMake](https://github.com/mseminatore/entity/actions/workflows/cmake.yml/badge.svg)](https://github.com/mseminatore/entity/actions/workflows/cmake.yml)

This lbirary is a small, header-only, Archetype-based Entity Component System (ECS) written using C++23.

`Entities` are cheap, generation-checked handles. `Components` are plain structs with no base
class. `Entities` are grouped into archetypes by their exact set of component types, so components of the same type are stored contiguously in memory for max cache
utilization. `Systems` are functions that iterate over a query/view that only touches matching archetypes.

## Key Features

- Header-only: `include/entity.h`
- Type-erased, archetype-based storage — no inheritance or virtual dispatch on components
- Automatic archetype interning: the same final component set always maps to the same
  archetype, regardless of the order components were added
- Cached archetype transitions for repeated `add`/`remove` calls
- Generation-checked entity handles — a stale handle safely fails `isAlive()` even after its index is recycled
- Simple query API (`view<Components...>()`) supporting both
  `func(Components&...)` and `func(Entity, Components&...)` callback versions, plus
  `.exclude<Excluded...>()` filtering
- It is safe to `add`/`remove`/`destroy` entities from inside a `for_each` callback. An entity destroyed or migrated mid-iteration by an earlier callback is skipped rather than read after it's invalidated.

## Minimum Requirements

- A C++23 compiler
- CMake 3.20+

## Getting started

This repository uses the [`testy`](https://github.com/mseminatore/testy) test framework as a git submodule, so clone with `--recursive` (or init it afterward):

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
#include "entity.h" // in include/

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

See `examples/main.cpp` for a more complete example (a small game loop with movement and collision systems).

## Unit Testing

Tests are run using CTest for local and CI testing. There are two targets:

- `entity_tests` — fast, basic correctness tests
- `entity_stress_tests` — larger-scale/randomized tests, tagged with the CTest label `stress`

```sh
cd build
ctest --output-on-failure       # run everything
ctest -LE stress                # fast suite only
ctest -L stress                 # stress suite only
```

Each test binary can be run directly for detailed test-case output:

```sh
./build/tests/entity_tests
./build/tests/entity_stress_tests
```

## Benchmarks

A micro benchmark lives under `benchmarks/`, covering entity create/destroy,
component add/remove migration chains, random component access, and `view().for_each()`
iteration. It's a manually run tool for measuring the cost of local changes to
`include/entity.h`/`include/archetype.h`. It doesn't run by default with CTest, because timing isn't a regular pass/fail criteria.

The `entity_bench` target always builds with `-O2`/`/O2` regardless of the top-level
configure (no `CMAKE_BUILD_TYPE` is set by default, so an unoptimized build would give meaningless numbers):

```sh
./build/benchmarks/entity_bench
```
