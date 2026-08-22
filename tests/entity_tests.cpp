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
	return em.isEntityAlive(e);
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
	return !em.isEntityAlive(e);
}

static bool test_recycled_index_gets_new_generation() {
	EntityManager em;
	Entity e1 = em.create();
	em.destroy(e1);
	Entity e2 = em.create();

	// index is reused, but generation advances so the old handle stays invalid
	return entityIndex(e1) == entityIndex(e2)
		&& e1 != e2
		&& !em.isEntityAlive(e1)
		&& em.isEntityAlive(e2);
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

	em.query<Position, Velocity>().for_each([&](Position& p, Velocity& v) {
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

	em.query<Position, Radius>().for_each([&](Position& p, Radius& r) {
		ok = ok && p.x == 1.0f && p.y == 2.0f && r.r == 5.0f;
	});

	return ok;
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
	em.query<Name>().for_each([&](Name& n) {
		ok = ok && n.value == "Rock";
	});

	return ok;
}

//------------------------------------------------------
// Query iteration
//------------------------------------------------------
static bool test_query_visits_all_matching_entities() {
	EntityManager em;
	std::vector<Entity> entities;

	for (int i = 0; i < 5; ++i) {
		Entity e = em.create();
		em.add<Position>(e, Position{ static_cast<float>(i), 0.0f });
		em.add<Velocity>(e, Velocity{ 1.0f, 0.0f });
		entities.push_back(e);
	}

	// an entity with only a Position should not match a Position+Velocity query
	Entity extra = em.create();
	em.add<Position>(extra, Position{ 100.0f, 100.0f });

	int visited = 0;
	em.query<Position, Velocity>().for_each([&](Position& p, Velocity& v) {
		p.x += v.vx; // mutate through the query
		++visited;
	});

	if (visited != 5) return false;

	// each original entity's position should have advanced by exactly one step,
	// and the unmatched extra entity should have been left untouched
	bool ok = true;
	for (int i = 0; i < 5; ++i) {
		bool found = false;
		em.query<Position>().for_each([&](Entity e, Position& p) {
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
static bool test_query_entity_aware_overload_visits_expected_entities() {
	EntityManager em;
	std::vector<Entity> expected;

	for (int i = 0; i < 4; ++i) {
		Entity e = em.create();
		em.add<Health>(e, Health{ static_cast<float>(i) * 10.0f });
		expected.push_back(e);
	}

	std::vector<Entity> visited;
	em.query<Health>().for_each([&](Entity e, Health& h) {
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

	bool ok = em.isEntityAlive(a) && !em.isEntityAlive(b) && em.isEntityAlive(c);

	em.query<Position>().for_each([&](Entity e, Position& p) {
		if (e == a) ok = ok && p.x == 1.0f && p.y == 1.0f;
		if (e == c) ok = ok && p.x == 3.0f && p.y == 3.0f;
	});

	return ok;
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

	SUITE("Non-trivial component types");
	TESTEX("a std::string component survives archetype migration", test_non_trivial_component_round_trips());

	SUITE("Query iteration");
	TESTEX("for_each visits exactly the matching entities", test_query_visits_all_matching_entities());
	TESTEX("entity-aware for_each visits the expected entities", test_query_entity_aware_overload_visits_expected_entities());

	SUITE("Removal / swap-remove semantics");
	TESTEX("destroying a middle entity preserves its siblings' data", test_destroy_preserves_sibling_component_data());
}
