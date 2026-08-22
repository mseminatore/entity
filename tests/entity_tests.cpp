//------------------------------------------------------
// Unit tests for the entity library, using the testy
// test framework (see testy/test.h).
//------------------------------------------------------
#include "test.h"

#include "entity.h"
#include "components.h"

#include <algorithm>
#include <vector>

//------------------------------------------------------
// Entity lifecycle
//------------------------------------------------------
static bool test_create_is_alive() {
	EntityManager em;
	Entity e = em.create();
	return em.isAlive(e);
}

static bool test_new_entity_has_no_components() {
	EntityManager em;
	Entity e = em.create();
	return !em.has<Position>(e);
}

static bool test_destroy_invalidates_entity() {
	EntityManager em;
	Entity e = em.create();
	em.destroy(e);
	return !em.isAlive(e);
}

static bool test_recycled_index_gets_new_generation() {
	EntityManager em;
	Entity e1 = em.create();
	em.destroy(e1);
	Entity e2 = em.create();

	// index is reused, but generation advances so the old handle stays invalid
	return entityIndex(e1) == entityIndex(e2)
		&& e1 != e2
		&& !em.isAlive(e1)
		&& em.isAlive(e2);
}

//------------------------------------------------------
// Component add / remove / has
//------------------------------------------------------
static bool test_add_sets_has() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, Position{ 1.0f, 2.0f });
	return em.has<Position>(e) && !em.has<Velocity>(e);
}

static bool test_add_returns_correct_value() {
	EntityManager em;
	Entity e = em.create();
	Position& p = em.add<Position>(e, Position{ 3.5f, -2.0f });
	return p.x == 3.5f && p.y == -2.0f;
}

static bool test_remove_clears_has() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, Position{ 1.0f, 1.0f });
	em.remove<Position>(e);
	return !em.has<Position>(e);
}

// adding a second component migrates the entity to a new archetype;
// verify the first component's data survives the move
static bool test_archetype_migration_preserves_earlier_component() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, Position{ 7.0f, 8.0f });
	em.add<Velocity>(e, Velocity{ 1.0f, -1.0f });

	bool ok = em.has<Position>(e) && em.has<Velocity>(e);

	em.view<Position, Velocity>().for_each([&](Position& p, Velocity& v) {
		ok = ok && p.x == 7.0f && p.y == 8.0f && v.vx == 1.0f && v.vy == -1.0f;
	});

	return ok;
}

// removing a middle component migrates the entity to a smaller archetype;
// verify the remaining components' data survives the move
static bool test_remove_preserves_other_components() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, Position{ 1.0f, 2.0f });
	em.add<Velocity>(e, Velocity{ 3.0f, 4.0f });
	em.add<Radius>(e, Radius{ 5.0f });

	em.remove<Velocity>(e);

	bool ok = em.has<Position>(e) && em.has<Radius>(e) && !em.has<Velocity>(e);

	em.view<Position, Radius>().for_each([&](Position& p, Radius& r) {
		ok = ok && p.x == 1.0f && p.y == 2.0f && r.r == 5.0f;
	});

	return ok;
}

//------------------------------------------------------
// Component accessors: get() / has()
//------------------------------------------------------
static bool test_has_returns_false_for_missing_component() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, Position{ 1.0f, 1.0f });

	return !em.has<Velocity>(e);
}

static bool test_get_returns_value_for_existing_component() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, Position{ 4.0f, 5.0f });

	auto pos = em.get<Position>(e);
	if (!pos || pos->get().x != 4.0f || pos->get().y != 5.0f) return false;

	// get() returns a reference into live storage; a mutation through it
	// must be visible to a subsequent get() call
	pos->get().x = 100.0f;
	auto pos2 = em.get<Position>(e);

	return pos2.has_value() && pos2->get().x == 100.0f;
}

static bool test_get_returns_nullopt_for_missing_component() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, Position{ 1.0f, 1.0f });

	auto vel = em.get<Velocity>(e);

	return !vel.has_value();
}

static bool test_get_const_overload_returns_value_and_nullopt() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, Position{ 2.0f, 3.0f });

	const EntityManager& cem = em;
	auto pos = cem.get<Position>(e);
	auto vel = cem.get<Velocity>(e);

	return pos.has_value() && pos->get().x == 2.0f && pos->get().y == 3.0f
		&& !vel.has_value();
}

//------------------------------------------------------
// Non-trivial component types (std::string member),
// which exercises the move-construct/destroy ComponentOps
// during archetype migrations rather than trivial memcpy.
//------------------------------------------------------
static bool test_non_trivial_component_round_trips() {
	EntityManager em;
	Entity e = em.create();
	em.add<Name>(e, Name{ "Rock" });

	// force an archetype migration; the Name column must be moved, not copied
	em.add<Position>(e, Position{ 0.0f, 0.0f });

	bool ok = true;
	em.view<Name>().for_each([&](Name& n) {
		ok = ok && n.value == "Rock";
	});

	return ok;
}

//------------------------------------------------------
// view iteration
//------------------------------------------------------
static bool test_view_visits_all_matching_entities() {
	EntityManager em;
	std::vector<Entity> entities;

	for (int i = 0; i < 5; ++i) {
		Entity e = em.create();
		em.add<Position>(e, Position{ static_cast<float>(i), 0.0f });
		em.add<Velocity>(e, Velocity{ 1.0f, 0.0f });
		entities.push_back(e);
	}

	// an entity with only a Position should not match a Position+Velocity view
	Entity extra = em.create();
	em.add<Position>(extra, Position{ 100.0f, 100.0f });

	int visited = 0;
	em.view<Position, Velocity>().for_each([&](Position& p, Velocity& v) {
		p.x += v.vx; // mutate through the view
		++visited;
	});

	if (visited != 5) return false;

	// each original entity's position should have advanced by exactly one step,
	// and the unmatched extra entity should have been left untouched
	bool ok = true;
	for (int i = 0; i < 5; ++i) {
		bool found = false;
		em.view<Position>().for_each([&](Entity e, Position& p) {
			if (e == entities[static_cast<std::size_t>(i)]) {
				found = true;
				ok = ok && (p.x == static_cast<float>(i) + 1.0f);
			}
			if (e == extra) {
				ok = ok && (p.x == 100.0f);
			}
		});
		ok = ok && found;
	}

	return ok;
}

// exercises the entity-aware for_each(Entity, Components&...) overload
static bool test_view_entity_aware_overload_visits_expected_entities() {
	EntityManager em;
	std::vector<Entity> expected;

	for (int i = 0; i < 4; ++i) {
		Entity e = em.create();
		em.add<Health>(e, Health{ static_cast<float>(i) * 10.0f });
		expected.push_back(e);
	}

	std::vector<Entity> visited;
	em.view<Health>().for_each([&](Entity e, Health& h) {
		visited.push_back(e);
		(void)h;
	});

	std::sort(expected.begin(), expected.end());
	std::sort(visited.begin(), visited.end());

	return expected == visited;
}

//------------------------------------------------------
// Removal / swap-remove semantics
//------------------------------------------------------
// removing an entity from the middle of an archetype's storage causes
// the last row to be swapped into its place; verify the surviving
// entities keep their own (not the swapped-in) component data
static bool test_destroy_preserves_sibling_component_data() {
	EntityManager em;
	Entity a = em.create();
	Entity b = em.create();
	Entity c = em.create();

	em.add<Position>(a, Position{ 1.0f, 1.0f });
	em.add<Position>(b, Position{ 2.0f, 2.0f });
	em.add<Position>(c, Position{ 3.0f, 3.0f });

	em.destroy(b); // middle entity; c's row should be swapped into b's old row

	bool ok = em.isAlive(a) && !em.isAlive(b) && em.isAlive(c);

	em.view<Position>().for_each([&](Entity e, Position& p) {
		if (e == a) ok = ok && p.x == 1.0f && p.y == 1.0f;
		if (e == c) ok = ok && p.x == 3.0f && p.y == 3.0f;
	});

	return ok;
}

//------------------------------------------------------
// Archetype identity & signature caching (white-box tests
// against ArchetypeRegistry directly, independent of EntityManager)
//------------------------------------------------------

// reaching the same final component set via addTarget in a different
// order must intern to the same archetype (canonical sorted signature)
static bool test_archetype_order_independent() {
	ArchetypeRegistry registry;
	Archetype& empty = registry.empty();

	ComponentId posId = ComponentType::get<Position>();
	ComponentId velId = ComponentType::get<Velocity>();

	Archetype& posThenVel = registry.addTarget(
		registry.addTarget(empty, posId, get_component_ops<Position>()),
		velId, get_component_ops<Velocity>());

	Archetype& velThenPos = registry.addTarget(
		registry.addTarget(empty, velId, get_component_ops<Velocity>()),
		posId, get_component_ops<Position>());

	return &posThenVel == &velThenPos;
}

// addEdge/removeEdge caching: repeated addTarget calls return the same
// archetype, and removeTarget is the true inverse of addTarget
static bool test_archetype_edge_caching_is_inverse() {
	ArchetypeRegistry registry;
	Archetype& empty = registry.empty();
	ComponentId posId = ComponentType::get<Position>();

	Archetype& withPos1 = registry.addTarget(empty, posId, get_component_ops<Position>());
	Archetype& withPos2 = registry.addTarget(empty, posId, get_component_ops<Position>());
	bool cached = (&withPos1 == &withPos2);

	Archetype& backToEmpty = registry.removeTarget(withPos1, posId);
	bool roundTrip = (&backToEmpty == &empty);

	return cached && roundTrip;
}

//------------------------------------------------------
// Column growth
//------------------------------------------------------
// force multiple Column::grow() doublings (MinCapacity=4, GrowthFactor=2)
// and verify every entity's data survived the reallocations intact
static bool test_many_entities_survive_column_growth() {
	EntityManager em;
	constexpr int N = 20; // comfortably past two growth doublings (4 -> 8 -> 16 -> 32)

	std::vector<Entity> entities;
	for (int i = 0; i < N; ++i) {
		Entity e = em.create();
		em.add<Position>(e, Position{ static_cast<float>(i), static_cast<float>(i) * 2.0f });
		em.add<Velocity>(e, Velocity{ static_cast<float>(i) * 0.5f, -static_cast<float>(i) });
		entities.push_back(e);
	}

	std::vector<bool> seen(N, false);
	bool ok = true;

	em.view<Position, Velocity>().for_each([&](Entity e, Position& p, Velocity& v) {
		for (int i = 0; i < N; ++i) {
			if (entities[static_cast<std::size_t>(i)] == e) {
				seen[static_cast<std::size_t>(i)] = true;
				ok = ok
					&& p.x == static_cast<float>(i) && p.y == static_cast<float>(i) * 2.0f
					&& v.vx == static_cast<float>(i) * 0.5f && v.vy == -static_cast<float>(i);
			}
		}
	});

	for (bool s : seen) ok = ok && s;

	return ok;
}

//------------------------------------------------------
// Archetype transitions edge cases
//------------------------------------------------------
static bool test_remove_only_component_returns_to_empty_archetype() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, Position{ 1.0f, 1.0f });
	em.remove<Position>(e);

	bool ok = !em.has<Position>(e) && em.isAlive(e);

	int visited = 0;
	em.view<>().for_each([&](Entity ent) {
		if (ent == e) ++visited;
	});

	return ok && visited == 1;
}

// destroying the last entity in an archetype's storage takes the
// row == last branch of finishRemovingRow (no swap needed)
static bool test_destroy_last_row_entity() {
	EntityManager em;
	Entity a = em.create();
	Entity b = em.create();

	em.add<Position>(a, Position{ 1.0f, 1.0f });
	em.add<Position>(b, Position{ 2.0f, 2.0f });

	em.destroy(b); // b is the last row in its archetype

	bool ok = em.isAlive(a) && !em.isAlive(b);

	em.view<Position>().for_each([&](Entity e, Position& p) {
		if (e == a) ok = ok && p.x == 1.0f && p.y == 1.0f;
	});

	return ok;
}

// a recycled entity index must start with a clean, empty component set
static bool test_recycled_entity_starts_with_no_components() {
	EntityManager em;
	Entity e1 = em.create();
	em.add<Position>(e1, Position{ 9.0f, 9.0f });
	em.destroy(e1);

	Entity e2 = em.create(); // likely reuses e1's index

	return entityIndex(e1) == entityIndex(e2) && !em.has<Position>(e2);
}

//------------------------------------------------------
// view edge cases
//------------------------------------------------------
static bool test_view_for_unused_component_visits_nothing() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, Position{ 1.0f, 1.0f });

	int visited = 0;
	em.view<Velocity>().for_each([&](Velocity&) { ++visited; });

	return visited == 0;
}

// EntityView<> with zero component types must visit every live entity
// across every archetype, including the "empty" (no components) one
static bool test_zero_arity_view_visits_every_entity() {
	EntityManager em;
	Entity withComponents = em.create();
	em.add<Position>(withComponents, Position{ 1.0f, 1.0f });

	Entity empty = em.create(); // stays in the empty archetype

	int visited = 0;
	bool sawWithComponents = false;
	bool sawEmpty = false;

	em.view<>().for_each([&](Entity e) {
		++visited;
		if (e == withComponents) sawWithComponents = true;
		if (e == empty) sawEmpty = true;
	});

	return visited == 2 && sawWithComponents && sawEmpty;
}

//------------------------------------------------------
// Integration-style scenarios (mirrors main.cpp's actual usage)
//------------------------------------------------------
// mirrors main.cpp's movementSystem: only entities with both
// Position and Velocity should move
static bool test_movement_system_only_moves_matching_entities() {
	EntityManager em;
	Entity moving = em.create();
	em.add<Position>(moving, Position{ 0.0f, 0.0f });
	em.add<Velocity>(moving, Velocity{ 1.0f, 2.0f });

	Entity still = em.create();
	em.add<Position>(still, Position{ 9.0f, 9.0f });

	em.view<Position, Velocity>().for_each([](Position& p, Velocity& v) {
		p.x += v.vx;
		p.y += v.vy;
	});

	bool ok = true;
	em.view<Position>().for_each([&](Entity e, Position& p) {
		if (e == moving) ok = ok && p.x == 1.0f && p.y == 2.0f;
		if (e == still)  ok = ok && p.x == 9.0f && p.y == 9.0f; // untouched
	});

	return ok;
}

// mirrors main.cpp's collisionSystem: a nested view().for_each() inside
// another, with a self-skip check via pointer comparison
static bool test_nested_view_iteration_skips_self_and_finds_pairs() {
	EntityManager em;
	Entity rock = em.create();
	em.add<Position>(rock, Position{ 0.0f, 0.0f });
	em.add<Radius>(rock, Radius{ 5.0f });
	em.add<Name>(rock, Name{ "Rock" });

	Entity missile = em.create();
	em.add<Position>(missile, Position{ 1.0f, 0.0f });
	em.add<Velocity>(missile, Velocity{ 0.0f, 0.0f });
	em.add<Radius>(missile, Radius{ 1.0f });
	em.add<Name>(missile, Name{ "Missile" });

	int pairs = 0;
	em.view<Position, Radius, Name>().for_each([&](Position& p1, Radius&, Name&) {
		em.view<Position, Velocity, Radius, Name>().for_each([&](Position& p2, Velocity&, Radius&, Name&) {
			if (&p1 == &p2) return; // skip self, mirrors main.cpp
			++pairs;
		});
	});

	// rock (outer) pairs with missile (inner, the only entity with Velocity);
	// missile-vs-missile in the inner pass is skipped as self
	return pairs == 1;
}

//------------------------------------------------------
// test entry point
//------------------------------------------------------
void test_main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	MODULE("Entity ECS library");

	SUITE("Entity lifecycle");
	TESTEX("create() produces a live entity", test_create_is_alive());
	TESTEX("a new entity has no components", test_new_entity_has_no_components());
	TESTEX("destroy() invalidates the entity", test_destroy_invalidates_entity());
	TESTEX("a recycled index gets a new generation", test_recycled_index_gets_new_generation());

	SUITE("Component add / remove / has");
	TESTEX("add() sets has() to true", test_add_sets_has());
	TESTEX("add() returns a reference to the stored value", test_add_returns_correct_value());
	TESTEX("remove() clears has()", test_remove_clears_has());
	TESTEX("archetype migration preserves earlier component data", test_archetype_migration_preserves_earlier_component());
	TESTEX("removing a component preserves the other components", test_remove_preserves_other_components());

	SUITE("Component accessors: get() / has()");
	TESTEX("has() returns false for a component that was never added", test_has_returns_false_for_missing_component());
	TESTEX("get() returns the live value for an existing component", test_get_returns_value_for_existing_component());
	TESTEX("get() returns std::nullopt for a missing component", test_get_returns_nullopt_for_missing_component());
	TESTEX("const get() returns a value or std::nullopt correctly", test_get_const_overload_returns_value_and_nullopt());

	SUITE("Non-trivial component types");
	TESTEX("a std::string component survives archetype migration", test_non_trivial_component_round_trips());

	SUITE("view iteration");
	TESTEX("for_each visits exactly the matching entities", test_view_visits_all_matching_entities());
	TESTEX("entity-aware for_each visits the expected entities", test_view_entity_aware_overload_visits_expected_entities());

	SUITE("Removal / swap-remove semantics");
	TESTEX("destroying a middle entity preserves its siblings' data", test_destroy_preserves_sibling_component_data());

	SUITE("Archetype identity & signature caching");
	TESTEX("adding components in a different order interns to the same archetype", test_archetype_order_independent());
	TESTEX("addTarget/removeTarget caching is a true inverse", test_archetype_edge_caching_is_inverse());

	SUITE("Column growth");
	TESTEX("many entities survive multiple column growth cycles", test_many_entities_survive_column_growth());

	SUITE("Archetype transitions edge cases");
	TESTEX("removing the only component returns to the empty archetype", test_remove_only_component_returns_to_empty_archetype());
	TESTEX("destroying the last row entity in an archetype", test_destroy_last_row_entity());
	TESTEX("a recycled entity starts with no components", test_recycled_entity_starts_with_no_components());

	SUITE("view edge cases");
	TESTEX("view for an unused component visits nothing", test_view_for_unused_component_visits_nothing());
	TESTEX("zero-arity view visits every entity", test_zero_arity_view_visits_every_entity());

	SUITE("Integration-style scenarios");
	TESTEX("movement system only moves matching entities", test_movement_system_only_moves_matching_entities());
	TESTEX("nested view iteration skips self and finds pairs", test_nested_view_iteration_skips_self_and_finds_pairs());
}
