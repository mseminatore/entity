//------------------------------------------------------
// Microbenchmarks for the entity library. A manually-run
// dev tool for measuring "before vs. after" on changes to
// entity.h/archetype.h -- not registered with ctest, since
// timing isn't a pass/fail signal.
//
// Run: ./build/benchmarks/entity_bench
//------------------------------------------------------
#include "entity.h"
#include "components.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace bench {

using Clock = std::chrono::steady_clock;

// Runs `setup()` (untimed) then times `body(state)` across `repeats`
// repetitions, keeping the minimum wall-clock time -- the most
// noise-resistant single statistic for a short-running microbenchmark on a
// shared machine. `body` must return a value derived from its own work
// (a checksum/accumulator), which gets folded into `sink` and printed, so
// the optimizer can't prove the loop's result is unobserved and elide it.
template <typename Setup, typename Body>
void run(const char* name, std::size_t opCount, int repeats, Setup setup, Body body) {
	double bestSeconds = -1.0;
	long long sink = 0;

	for (int r = 0; r < repeats; ++r) {
		auto state = setup();

		auto start = Clock::now();
		sink += body(state);
		auto end = Clock::now();

		double seconds = std::chrono::duration<double>(end - start).count();
		if (bestSeconds < 0.0 || seconds < bestSeconds)
			bestSeconds = seconds;
	}

	double nsPerOp = (bestSeconds * 1e9) / static_cast<double>(opCount);
	double opsPerSec = static_cast<double>(opCount) / bestSeconds;

	std::printf("%-60s %11zu ops  %10.1f ns/op  %13.0f ops/sec  [checksum %lld]\n",
		name, opCount, nsPerOp, opsPerSec, sink);
}

} // namespace bench

namespace {

// shared "manager + a list of entities already in it" state, reused by
// most benchmarks below whose timed body operates on pre-existing entities
struct ManagerAndEntities {
	EntityManager em;
	std::vector<Entity> entities;
};

constexpr std::size_t N_CREATE = 1'000'000;
constexpr std::size_t N_DESTROY = 1'000'000;
constexpr std::size_t N_CHURN = 1'000'000;
constexpr std::size_t N_ADDCHAIN = 200'000;
constexpr std::size_t N_REMOVECHAIN = 200'000;
constexpr std::size_t N_RANDOMACCESS = 1'000'000;
constexpr std::size_t N_FOREACH = 1'000'000;
constexpr std::size_t N_FOREACH_FRAGMENTED = 1'000'000;
constexpr unsigned FragmentedOtherTypeCount = 4; // Radius, Health, Bounds, Name (Position and Velocity are on every archetype, to match the single-archetype benchmark's per-entity work)
constexpr unsigned FragmentedArchetypeCount = 1u << FragmentedOtherTypeCount; // 16
constexpr std::size_t NumRocks = 200;
constexpr std::size_t NumMissiles = 200;
constexpr std::size_t NumRocksLarge = 2'000;
constexpr std::size_t NumMissilesLarge = 2'000;
constexpr int Repeats = 5;

void bench_create() {
	bench::run("create(): bulk create N entities", N_CREATE, Repeats,
		[]() { return EntityManager{}; },
		[](EntityManager& em) -> long long {
			long long sink = 0;
			for (std::size_t i = 0; i < N_CREATE; ++i) {
				Entity e = em.create();
				sink += static_cast<long long>(entityIndex(e));
			}
			return sink;
		});
}

void bench_create_reserved() {
	bench::run("create(): bulk create N entities (reserved)", N_CREATE, Repeats,
		[]() {
			EntityManager em;
			em.reserve(N_CREATE);
			return em;
		},
		[](EntityManager& em) -> long long {
			long long sink = 0;
			for (std::size_t i = 0; i < N_CREATE; ++i) {
				Entity e = em.create();
				sink += static_cast<long long>(entityIndex(e));
			}
			return sink;
		});
}

void bench_destroy() {
	bench::run("destroy(): bulk destroy N pre-created entities", N_DESTROY, Repeats,
		[]() {
			ManagerAndEntities state;
			state.entities.reserve(N_DESTROY);
			for (std::size_t i = 0; i < N_DESTROY; ++i)
				state.entities.push_back(state.em.create());
			return state;
		},
		[](ManagerAndEntities& state) -> long long {
			for (Entity e : state.entities)
				state.em.destroy(e);
			return static_cast<long long>(state.entities.size());
		});
}

void bench_churn() {
	bench::run("create()+destroy(): recycle churn", N_CHURN, Repeats,
		[]() { return EntityManager{}; },
		[](EntityManager& em) -> long long {
			long long sink = 0;
			for (std::size_t i = 0; i < N_CHURN; ++i) {
				Entity e = em.create();
				sink += static_cast<long long>(entityIndex(e));
				em.destroy(e);
			}
			return sink;
		});
}

void bench_add_chain() {
	bench::run("add<T>(): 4-component build-up chain (per add() call)", N_ADDCHAIN * 4, Repeats,
		[]() {
			ManagerAndEntities state;
			state.entities.reserve(N_ADDCHAIN);
			for (std::size_t i = 0; i < N_ADDCHAIN; ++i)
				state.entities.push_back(state.em.create());
			return state;
		},
		[](ManagerAndEntities& state) -> long long {
			long long sink = 0;
			for (std::size_t i = 0; i < state.entities.size(); ++i) {
				Entity e = state.entities[i];
				float v = static_cast<float>(i);
				state.em.add<Position>(e, Position{ v, v });
				state.em.add<Velocity>(e, Velocity{ v, -v });
				state.em.add<Radius>(e, Radius{ v });
				state.em.add<Name>(e, Name{ "e" + std::to_string(i) });
				sink += static_cast<long long>(state.em.has<Name>(e));
			}
			return sink;
		});
}

void bench_remove_chain() {
	bench::run("remove<T>(): 4-component teardown chain (per remove() call)", N_REMOVECHAIN * 4, Repeats,
		[]() {
			ManagerAndEntities state;
			state.entities.reserve(N_REMOVECHAIN);
			for (std::size_t i = 0; i < N_REMOVECHAIN; ++i) {
				Entity e = state.em.create();
				float v = static_cast<float>(i);
				state.em.add<Position>(e, Position{ v, v });
				state.em.add<Velocity>(e, Velocity{ v, -v });
				state.em.add<Radius>(e, Radius{ v });
				state.em.add<Name>(e, Name{ "e" + std::to_string(i) });
				state.entities.push_back(e);
			}
			return state;
		},
		[](ManagerAndEntities& state) -> long long {
			long long sink = 0;
			for (Entity e : state.entities) {
				state.em.remove<Name>(e);
				state.em.remove<Radius>(e);
				state.em.remove<Velocity>(e);
				state.em.remove<Position>(e);
				sink += static_cast<long long>(state.em.isAlive(e));
			}
			return sink;
		});
}

void bench_random_access() {
	bench::run("get<T>()/has<T>(): shuffled random access", N_RANDOMACCESS, Repeats,
		[]() {
			ManagerAndEntities state;
			state.entities.reserve(N_RANDOMACCESS);
			for (std::size_t i = 0; i < N_RANDOMACCESS; ++i) {
				Entity e = state.em.create();
				state.em.add<Position>(e, Position{ static_cast<float>(i), static_cast<float>(i) });
				if (i % 2 == 0)
					state.em.add<Velocity>(e, Velocity{ 1.0f, 1.0f });
				state.entities.push_back(e);
			}
			std::mt19937 rng(7); // fixed seed: deterministic and reproducible across runs
			std::shuffle(state.entities.begin(), state.entities.end(), rng);
			return state;
		},
		[](ManagerAndEntities& state) -> long long {
			double sum = 0.0;
			for (Entity e : state.entities) {
				if (state.em.has<Position>(e)) {
					auto p = state.em.get<Position>(e);
					sum += p->get().x;
				}
			}
			return static_cast<long long>(sum);
		});
}

void bench_foreach_single_archetype() {
	bench::run("view().for_each(): single archetype", N_FOREACH, Repeats,
		[]() {
			ManagerAndEntities state;
			state.entities.reserve(N_FOREACH);
			for (std::size_t i = 0; i < N_FOREACH; ++i) {
				Entity e = state.em.create();
				state.em.add<Position>(e, Position{ static_cast<float>(i), 0.0f });
				state.em.add<Velocity>(e, Velocity{ 1.0f, 1.0f });
				state.entities.push_back(e);
			}
			return state;
		},
		[](ManagerAndEntities& state) -> long long {
			double sum = 0.0;
			state.em.view<Position, Velocity>().for_each([&](Position& p, Velocity& v) {
				p.x += v.vx;
				sum += p.x;
			});
			return static_cast<long long>(sum);
		});
}

void bench_foreach_fragmented() {
	bench::run("view().for_each(): fragmented across 16 archetypes", N_FOREACH_FRAGMENTED, Repeats,
		[]() {
			ManagerAndEntities state;
			std::size_t perArchetype = N_FOREACH_FRAGMENTED / FragmentedArchetypeCount;
			state.entities.reserve(perArchetype * FragmentedArchetypeCount);

			for (unsigned mask = 0; mask < FragmentedArchetypeCount; ++mask) {
				for (std::size_t i = 0; i < perArchetype; ++i) {
					Entity e = state.em.create();
					float v = static_cast<float>(i);
					// Position and Velocity are on every archetype here (matching the
					// single-archetype benchmark's per-entity work), so the only
					// difference between the two benchmarks is archetype fan-out.
					state.em.add<Position>(e, Position{ v, 0.0f });
					state.em.add<Velocity>(e, Velocity{ 1.0f, 1.0f });
					if (mask & 1u) state.em.add<Radius>(e, Radius{ v });
					if (mask & 2u) state.em.add<Health>(e, Health{ v });
					if (mask & 4u) state.em.add<Bounds>(e, Bounds{ v, v });
					if (mask & 8u) state.em.add<Name>(e, Name{ "e" });
					state.entities.push_back(e);
				}
			}
			return state;
		},
		[](ManagerAndEntities& state) -> long long {
			double sum = 0.0;
			state.em.view<Position, Velocity>().for_each([&](Position& p, Velocity& v) {
				p.x += v.vx;
				sum += p.x;
			});
			return static_cast<long long>(sum);
		});
}

// `numRocks`/`numMissiles` scale the O(R*M) shape -- run at both a small scale (matching
// main.cpp's demo counts) and a larger one, since the per-op ns/op at 200/200 is dominated
// by the whole working set fitting in cache and doesn't show how the snapshot-allocation-
// per-outer-iteration cost (EntityView::for_each, entity.h) and the O(R*M) growth actually
// compound once entity counts approach what a real game's broad-phase might see.
void bench_nested_view(const char* name, std::size_t numRocks, std::size_t numMissiles) {
	std::size_t opCount = (numRocks + numMissiles) * numMissiles; // total inner-loop invocations

	bench::run(name, opCount, Repeats,
		[=]() {
			ManagerAndEntities state;
			for (std::size_t i = 0; i < numRocks; ++i) {
				Entity e = state.em.create();
				state.em.add<Position>(e, Position{ static_cast<float>(i), 0.0f });
				state.em.add<Radius>(e, Radius{ 1.0f });
				state.em.add<Name>(e, Name{ "Rock" });
			}
			for (std::size_t i = 0; i < numMissiles; ++i) {
				Entity e = state.em.create();
				state.em.add<Position>(e, Position{ static_cast<float>(i), 1.0f });
				state.em.add<Velocity>(e, Velocity{ 0.0f, 0.0f });
				state.em.add<Radius>(e, Radius{ 1.0f });
				state.em.add<Name>(e, Name{ "Missile" });
			}
			return state;
		},
		[](ManagerAndEntities& state) -> long long {
			long long invocations = 0;
			state.em.view<Position, Radius, Name>().for_each([&](Position& p1, Radius&, Name&) {
				state.em.view<Position, Velocity, Radius, Name>().for_each([&](Position& p2, Velocity&, Radius&, Name&) {
					++invocations;
					if (&p1 == &p2) return; // skip self, mirrors main.cpp's collisionSystem
				});
			});
			return invocations;
		});
}

} // namespace

int main() {
	std::printf("entity ECS microbenchmarks\n");
	std::printf("---------------------------------------------------------------------------------------------------------------\n");

	bench_create();
	bench_create_reserved();
	bench_destroy();
	bench_churn();
	bench_add_chain();
	bench_remove_chain();
	bench_random_access();
	bench_foreach_single_archetype();
	bench_foreach_fragmented();
	bench_nested_view("nested view(): collision-shaped O(R*M) iteration (200 rocks/200 missiles)", NumRocks, NumMissiles);
	bench_nested_view("nested view(): collision-shaped O(R*M) iteration (2000 rocks/2000 missiles)", NumRocksLarge, NumMissilesLarge);

	return 0;
}
