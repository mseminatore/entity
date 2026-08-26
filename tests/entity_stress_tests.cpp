//------------------------------------------------------
// Stress tests for the entity library, using the testy
// test framework (see testy/test.h). Kept in a separate
// executable/CTest target from entity_tests so the fast
// everyday suite stays fast; these are for full/nightly
// CI runs (see tests/CMakeLists.txt, CTest LABEL "stress").
//------------------------------------------------------
#include "test.h"

#include "entity.h"
#include "components.h"

#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//------------------------------------------------------
// Randomized churn against a shadow model: thousands of
// randomized create/destroy/add/remove/mutate operations,
// cross-checked at intervals against a plain-data mirror
// of what EntityManager should report. This is the test
// most likely to catch state corruption under realistic
// mixed traffic, as opposed to one hand-picked scenario.
//------------------------------------------------------
namespace {

struct ShadowEntity {
	std::optional<Position> position;
	std::optional<Velocity> velocity;
	std::optional<Health> health;
	std::optional<std::string> name;
	std::optional<Radius> radius;
	std::optional<Bounds> bounds;
};

bool cross_check(EntityManager& em, Entity e, const ShadowEntity& shadow) {
	if (!em.isAlive(e)) return false;

	bool hasPos = em.has<Position>(e);
	if (hasPos != shadow.position.has_value()) return false;
	if (hasPos) {
		auto p = em.get<Position>(e);
		if (!p || p->get().x != shadow.position->x || p->get().y != shadow.position->y) return false;
	}

	bool hasVel = em.has<Velocity>(e);
	if (hasVel != shadow.velocity.has_value()) return false;
	if (hasVel) {
		auto v = em.get<Velocity>(e);
		if (!v || v->get().vx != shadow.velocity->vx || v->get().vy != shadow.velocity->vy) return false;
	}

	bool hasHealth = em.has<Health>(e);
	if (hasHealth != shadow.health.has_value()) return false;
	if (hasHealth) {
		auto h = em.get<Health>(e);
		if (!h || h->get().hp != shadow.health->hp) return false;
	}

	// Name (std::string) participates in churn so the move-construct/destroy
	// ComponentOps path for a non-trivial type gets exercised by the fuzzer too
	bool hasName = em.has<Name>(e);
	if (hasName != shadow.name.has_value()) return false;
	if (hasName) {
		auto n = em.get<Name>(e);
		if (!n || n->get().value != *shadow.name) return false;
	}

	bool hasRadius = em.has<Radius>(e);
	if (hasRadius != shadow.radius.has_value()) return false;
	if (hasRadius) {
		auto r = em.get<Radius>(e);
		if (!r || r->get().r != shadow.radius->r) return false;
	}

	bool hasBounds = em.has<Bounds>(e);
	if (hasBounds != shadow.bounds.has_value()) return false;
	if (hasBounds) {
		auto b = em.get<Bounds>(e);
		if (!b || b->get().width != shadow.bounds->width || b->get().height != shadow.bounds->height) return false;
	}

	return true;
}

} // namespace

static bool test_randomized_churn_matches_shadow_model() {
	EntityManager em;
	std::unordered_map<Entity, ShadowEntity> shadow;
	std::vector<Entity> alive;

	std::mt19937 rng(12345); // fixed seed: deterministic and reproducible in CI
	std::uniform_int_distribution<int> opDist(0, 8);
	std::uniform_real_distribution<float> valDist(-1000.0f, 1000.0f);

	constexpr int Iterations = 5000;

	for (int iter = 0; iter < Iterations; ++iter) {
		int op = opDist(rng);

		if (op == 0) {
			Entity e = em.create();
			shadow[e] = ShadowEntity{};
			alive.push_back(e);
		} else if (!alive.empty()) {
			std::uniform_int_distribution<std::size_t> pick(0, alive.size() - 1);
			std::size_t idx = pick(rng);
			Entity e = alive[idx];
			ShadowEntity& s = shadow[e];

			switch (op) {
			case 1: // destroy
				em.destroy(e);
				shadow.erase(e);
				alive[idx] = alive.back();
				alive.pop_back();
				break;
			case 2: // add or remove Position
				if (!s.position) {
					Position p{ valDist(rng), valDist(rng) };
					em.add<Position>(e, p);
					s.position = p;
				} else {
					em.remove<Position>(e);
					s.position.reset();
				}
				break;
			case 3: // add or remove Velocity
				if (!s.velocity) {
					Velocity v{ valDist(rng), valDist(rng) };
					em.add<Velocity>(e, v);
					s.velocity = v;
				} else {
					em.remove<Velocity>(e);
					s.velocity.reset();
				}
				break;
			case 4: // add or remove Health
				if (!s.health) {
					Health h{ valDist(rng) };
					em.add<Health>(e, h);
					s.health = h;
				} else {
					em.remove<Health>(e);
					s.health.reset();
				}
				break;
			case 5: // add or remove Name (the only non-trivial/string component type)
				if (!s.name) {
					std::string n = "entity_" + std::to_string(entityIndex(e)) + "_" + std::to_string(iter);
					em.add<Name>(e, Name{ n });
					s.name = n;
				} else {
					em.remove<Name>(e);
					s.name.reset();
				}
				break;
			case 6: // add or remove Radius
				if (!s.radius) {
					Radius r{ valDist(rng) };
					em.add<Radius>(e, r);
					s.radius = r;
				} else {
					em.remove<Radius>(e);
					s.radius.reset();
				}
				break;
			case 7: // add or remove Bounds
				if (!s.bounds) {
					Bounds b{ valDist(rng), valDist(rng) };
					em.add<Bounds>(e, b);
					s.bounds = b;
				} else {
					em.remove<Bounds>(e);
					s.bounds.reset();
				}
				break;
			case 8: // mutate an existing component in place through get()
				if (s.position) {
					auto p = em.get<Position>(e);
					if (!p) return false;
					p->get().x = valDist(rng);
					s.position->x = p->get().x;
				}
				break;
			}
		}

		// periodic full cross-check against the shadow model
		if (iter % 500 == 499) {
			if (alive.size() != em.size()) return false;
			for (Entity e : alive) {
				if (!cross_check(em, e, shadow.at(e))) return false;
			}
		}
	}

	if (alive.size() != em.size()) return false;
	for (Entity e : alive) {
		if (!cross_check(em, e, shadow.at(e))) return false;
	}

	return true;
}

//------------------------------------------------------
// Large-scale column growth: push Column::grow() through
// many more doublings than the small (N=20) version of
// this test in entity_tests.cpp, verifying no data
// corruption across the larger set of reallocations.
//------------------------------------------------------
static bool test_large_scale_column_growth() {
	EntityManager em;
	constexpr int N = 5000;

	std::vector<Entity> entities;
	entities.reserve(N);
	std::unordered_map<Entity, int> indexOf;
	indexOf.reserve(N * 2);

	for (int i = 0; i < N; ++i) {
		Entity e = em.create();
		em.add<Position>(e, Position{ static_cast<float>(i), static_cast<float>(i) * 2.0f });
		em.add<Velocity>(e, Velocity{ static_cast<float>(i) * 0.5f, -static_cast<float>(i) });
		entities.push_back(e);
		indexOf[e] = i;
	}

	std::vector<bool> seen(N, false);
	bool ok = true;
	int visitedCount = 0;

	em.view<Position, Velocity>().for_each([&](Entity e, Position& p, Velocity& v) {
		++visitedCount;
		auto it = indexOf.find(e);
		if (it == indexOf.end()) { ok = false; return; }

		int i = it->second;
		seen[static_cast<std::size_t>(i)] = true;
		ok = ok
			&& p.x == static_cast<float>(i) && p.y == static_cast<float>(i) * 2.0f
			&& v.vx == static_cast<float>(i) * 0.5f && v.vy == -static_cast<float>(i);
	});

	if (visitedCount != N) ok = false;
	for (bool s : seen) ok = ok && s;

	return ok;
}

//------------------------------------------------------
// Large-scale column growth, non-trivial component: the same
// reallocation stress as above, but for Name (std::string),
// where a use-after-move/double-destroy bug in the move-construct
// path is most likely to surface at scale.
//------------------------------------------------------
static bool test_large_scale_column_growth_non_trivial_component() {
	EntityManager em;
	constexpr int N = 5000;

	std::vector<Entity> entities;
	entities.reserve(N);
	std::unordered_map<Entity, int> indexOf;
	indexOf.reserve(N * 2);

	for (int i = 0; i < N; ++i) {
		Entity e = em.create();
		em.add<Name>(e, Name{ "entity_" + std::to_string(i) });
		entities.push_back(e);
		indexOf[e] = i;
	}

	std::vector<bool> seen(N, false);
	bool ok = true;
	int visitedCount = 0;

	em.view<Name>().for_each([&](Entity e, Name& n) {
		++visitedCount;
		auto it = indexOf.find(e);
		if (it == indexOf.end()) { ok = false; return; }

		int i = it->second;
		seen[static_cast<std::size_t>(i)] = true;
		ok = ok && n.value == ("entity_" + std::to_string(i));
	});

	if (visitedCount != N) ok = false;
	for (bool s : seen) ok = ok && s;

	return ok;
}

//------------------------------------------------------
// Archetype explosion: reach every non-empty subset of
// all 6 component types (63 distinct signatures) via two
// different add orders each, and confirm both orders land
// on the identical archetype and the registry never creates
// a duplicate for a signature it has already interned.
//------------------------------------------------------
static bool test_archetype_explosion_no_duplicate_archetypes() {
	ArchetypeRegistry registry;
	constexpr unsigned NumTypes = 6;
	constexpr unsigned NumMasks = (1u << NumTypes) - 1; // 63 non-empty subsets

	ComponentId ids[NumTypes] = {
		ComponentType::get<Position>(),
		ComponentType::get<Velocity>(),
		ComponentType::get<Radius>(),
		ComponentType::get<Health>(),
		ComponentType::get<Bounds>(),
		ComponentType::get<Name>(),
	};
	const ComponentOps* ops[NumTypes] = {
		get_component_ops<Position>(),
		get_component_ops<Velocity>(),
		get_component_ops<Radius>(),
		get_component_ops<Health>(),
		get_component_ops<Bounds>(),
		get_component_ops<Name>(),
	};

	std::unordered_set<Archetype*> distinctArchetypes;

	for (unsigned mask = 1; mask <= NumMasks; ++mask) {
		Archetype* forward = &registry.empty();
		for (unsigned bit = 0; bit < NumTypes; ++bit) {
			if (mask & (1u << bit)) forward = &registry.addTarget(*forward, ids[bit], ops[bit]);
		}

		Archetype* reverse = &registry.empty();
		for (unsigned bit = NumTypes; bit-- > 0; ) {
			if (mask & (1u << bit)) reverse = &registry.addTarget(*reverse, ids[bit], ops[bit]);
		}

		if (forward != reverse) return false;
		distinctArchetypes.insert(forward);
	}

	// 63 distinct non-empty subsets, plus the registry's own empty archetype
	return distinctArchetypes.size() == NumMasks && registry.all().size() == NumMasks + 1;
}

//------------------------------------------------------
// Archetype explosion with real data: the test above only checks archetype
// identity/count against a bare ArchetypeRegistry. This populates one real,
// live entity per one of the 63 non-empty component-subsets with distinct
// per-mask values, and verifies both direct get<T>()/has<T>() and view()-based
// round-trip, so data integrity is actually checked across many simultaneously
// live, densely populated archetypes.
//------------------------------------------------------
static bool test_archetype_explosion_entities_round_trip_via_view() {
	EntityManager em;
	constexpr unsigned NumTypes = 6;
	constexpr unsigned NumMasks = (1u << NumTypes) - 1; // 63 non-empty subsets

	std::vector<Entity> entities(NumMasks + 1, NullEntity); // 1-indexed by mask

	for (unsigned mask = 1; mask <= NumMasks; ++mask) {
		Entity e = em.create();
		float v = static_cast<float>(mask);

		if (mask & (1u << 0)) em.add<Position>(e, Position{ v, v });
		if (mask & (1u << 1)) em.add<Velocity>(e, Velocity{ v, v });
		if (mask & (1u << 2)) em.add<Radius>(e, Radius{ v });
		if (mask & (1u << 3)) em.add<Health>(e, Health{ v });
		if (mask & (1u << 4)) em.add<Bounds>(e, Bounds{ v, v });
		if (mask & (1u << 5)) em.add<Name>(e, Name{ "mask_" + std::to_string(mask) });

		entities[mask] = e;
	}

	bool ok = true;
	for (unsigned mask = 1; mask <= NumMasks; ++mask) {
		Entity e = entities[mask];
		float v = static_cast<float>(mask);

		bool wantPos = (mask & (1u << 0)) != 0;
		bool wantVel = (mask & (1u << 1)) != 0;
		bool wantRadius = (mask & (1u << 2)) != 0;
		bool wantHealth = (mask & (1u << 3)) != 0;
		bool wantBounds = (mask & (1u << 4)) != 0;
		bool wantName = (mask & (1u << 5)) != 0;

		ok = ok && em.has<Position>(e) == wantPos;
		ok = ok && em.has<Velocity>(e) == wantVel;
		ok = ok && em.has<Radius>(e) == wantRadius;
		ok = ok && em.has<Health>(e) == wantHealth;
		ok = ok && em.has<Bounds>(e) == wantBounds;
		ok = ok && em.has<Name>(e) == wantName;

		if (wantPos) { auto p = em.get<Position>(e); ok = ok && p && p->get().x == v && p->get().y == v; }
		if (wantVel) { auto vv = em.get<Velocity>(e); ok = ok && vv && vv->get().vx == v && vv->get().vy == v; }
		if (wantRadius) { auto r = em.get<Radius>(e); ok = ok && r && r->get().r == v; }
		if (wantHealth) { auto h = em.get<Health>(e); ok = ok && h && h->get().hp == v; }
		if (wantBounds) { auto b = em.get<Bounds>(e); ok = ok && b && b->get().width == v && b->get().height == v; }
		if (wantName) { auto n = em.get<Name>(e); ok = ok && n && n->get().value == ("mask_" + std::to_string(mask)); }
	}

	// cross-check via view(): every entity whose mask includes bit 0 (Position)
	// must be found by view<Position>(), and the count must match exactly
	int expectedWithPosition = 0;
	for (unsigned mask = 1; mask <= NumMasks; ++mask) {
		if (mask & 1u) ++expectedWithPosition;
	}

	int visitedWithPosition = 0;
	em.view<Position>().for_each([&](Entity, Position&) { ++visitedWithPosition; });
	ok = ok && visitedWithPosition == expectedWithPosition;

	// view<>() (visit-all) must see every one of the 63 live entities
	int visitedAll = 0;
	em.view<>().for_each([&](Entity) { ++visitedAll; });
	ok = ok && visitedAll == static_cast<int>(NumMasks);

	return ok;
}

//------------------------------------------------------
// Large nested iteration: a bigger version of the
// collisionSystem-shaped nested-view test in entity_tests.cpp,
// with the expected pair count derived combinatorially rather
// than from a single hand-picked pair.
//------------------------------------------------------
static bool test_large_nested_iteration_matches_reference() {
	EntityManager em;
	constexpr int NumRocks = 80;
	constexpr int NumMissiles = 80;

	for (int i = 0; i < NumRocks; ++i) {
		Entity e = em.create();
		em.add<Position>(e, Position{ static_cast<float>(i), 0.0f });
		em.add<Radius>(e, Radius{ 1.0f });
		em.add<Name>(e, "Rock");
	}

	for (int i = 0; i < NumMissiles; ++i) {
		Entity e = em.create();
		em.add<Position>(e, Position{ static_cast<float>(i), 1.0f });
		em.add<Velocity>(e, Velocity{ 0.0f, 0.0f });
		em.add<Radius>(e, Radius{ 1.0f });
		em.add<Name>(e, "Missile");
	}

	long long pairs = 0;
	em.view<Position, Radius, Name>().for_each([&](Position& p1, Radius&, Name&) {
		em.view<Position, Velocity, Radius, Name>().for_each([&](Position& p2, Velocity&, Radius&, Name&) {
			if (&p1 == &p2) return; // skip self, mirrors main.cpp's collisionSystem
			++pairs;
		});
	});

	// the outer view matches every rock and every missile (both have Position,
	// Radius, Name); the inner view matches only missiles (Velocity required).
	// every outer/inner combination counts except a missile paired with itself.
	long long expected = static_cast<long long>(NumRocks + NumMissiles) * NumMissiles - NumMissiles;

	return pairs == expected;
}

//------------------------------------------------------
// test entry point
//------------------------------------------------------
void test_main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	MODULE("Entity ECS library (stress)");

	SUITE("Randomized churn");
	TESTEX("randomized create/destroy/add/remove/mutate matches a shadow model", test_randomized_churn_matches_shadow_model());

	SUITE("Large-scale column growth");
	TESTEX("5000 entities survive many Column::grow() doublings", test_large_scale_column_growth());
	TESTEX("5000 Name (std::string) entities survive many Column::grow() doublings", test_large_scale_column_growth_non_trivial_component());

	SUITE("Archetype explosion");
	TESTEX("all 63 non-empty subsets of 6 component types intern without duplicates", test_archetype_explosion_no_duplicate_archetypes());
	TESTEX("all 63 subsets round-trip real entity data correctly via get()/view()", test_archetype_explosion_entities_round_trip_via_view());

	SUITE("Large nested iteration");
	TESTEX("160-entity nested view iteration matches the combinatorial reference count", test_large_nested_iteration_matches_reference());
}
