# entity

[![CMake](https://github.com/mseminatore/entity/actions/workflows/cmake.yml/badge.svg)](https://github.com/mseminatore/entity/actions/workflows/cmake.yml)

A small, header-only, archetype-based Entity Component System (ECS) for C++23.

Entities are cheap, generation-checked handles. Components are plain structs with no base
class or registration boilerplate. Entities are grouped into archetypes by their exact set
of component types, so components of the same type are stored contiguously and iteration
over a query only touches matching archetypes.

## Features

- Header-only: `entity.h`, `archetype.h`, `component_props.h`
- Type-erased, archetype-based storage — no inheritance or virtual dispatch on components
- Automatic archetype interning: the same final component set always maps to the same
  archetype, regardless of the order components were added in
- Cached archetype transitions for repeated `add`/`remove` calls
- Generation-checked entity handles — a stale handle safely fails `isAlive()` even after
  its index is recycled
- Simple query API (`view<Components...>()`) supporting both
  `func(Components&...)` and `func(Entity, Components&...)` callback shapes

## Requirements

- A C++23 compiler
- CMake 3.10+

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

This produces the `entity` demo executable and the test binaries under `build/tests`.

## Usage

```cpp
#include "entity.h"

struct Position { float x, y; };
struct Velocity { float vx, vy; };

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

// query for a single entity's component
if (auto pos = entityManager.get<Position>(e)) {
    printf("x=%f y=%f\n", pos->get().x, pos->get().y);
}

entityManager.remove<Velocity>(e);
entityManager.destroy(e);
```

See `main.cpp` for a fuller example (a small game loop with movement and collision systems).

## Testing

Tests run via CTest and are split into two targets:

- `entity_tests` — the fast, everyday correctness suite
- `entity_stress_tests` — larger-scale/randomized tests, tagged with the CTest label `stress`

```sh
cd build
ctest --output-on-failure       # run everything
ctest -LE stress                # fast suite only
ctest -L stress                 # stress suite only
```

Each test binary can also be run directly for colored, per-case output:

```sh
./build/tests/entity_tests
./build/tests/entity_stress_tests
```

## License

MIT — see [LICENSE](LICENSE).
