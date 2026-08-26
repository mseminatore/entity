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

// a handle whose index was never allocated must fail isAlive() via the
// bounds-check branch, not just the generation-mismatch branch
static bool test_isAlive_false_for_out_of_range_index() {
	EntityManager em;
	Entity fabricated = makeEntity(9999, 0);
	return !em.isAlive(fabricated);
}

// cycling destroy()/create() on the same index several times must keep
// advancing the generation, and every previously-issued stale handle for
// that index (not just the immediately-prior one) must stay invalid
static bool test_generation_bump_across_multiple_recycle_cycles() {
	EntityManager em;
	std::vector<Entity> handles;

	Entity cur = em.create();
	handles.push_back(cur);

	for (int i = 0; i < 3; ++i) {
		em.destroy(cur);
		cur = em.create();
		handles.push_back(cur);
	}

	bool ok = em.isAlive(cur);
	for (std::size_t i = 0; i + 1 < handles.size(); ++i) {
		ok = ok && !em.isAlive(handles[i]);
		ok = ok && entityIndex(handles[i]) == entityIndex(cur);
	}

	return ok;
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
// add() forwarding construction: main.cpp calls add<T>()
// by forwarding raw constructor arguments (e.g. a const
// char* into a C++20 parenthesized-aggregate-init), not by
// pre-building a T and moving/copying it in like most tests
// above do. Exercise that real call shape directly.
//------------------------------------------------------
static bool test_add_forwards_single_constructor_arg() {
	EntityManager em;
	Entity e = em.create();
	em.add<Name>(e, "Rock"); // matches main.cpp's add<Name>(rock, "Rock")

	auto name = em.get<Name>(e);
	return name.has_value() && name->get().value == "Rock";
}

static bool test_add_forwards_multiple_constructor_args() {
	EntityManager em;
	Entity e = em.create();
	em.add<Position>(e, 5.0f, 6.0f); // proves Args&&... forwards more than one argument

	auto pos = em.get<Position>(e);
	return pos.has_value() && pos->get().x == 5.0f && pos->get().y == 6.0f;
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

// remove<T>() migrates the entity to a smaller archetype too; the Name
// column's std::string value must survive that move-construct/destroy path
// just as it does for add<T>() above
static bool test_remove_preserves_non_trivial_component() {
	EntityManager em;
	Entity e = em.create();
	em.add<Name>(e, Name{ "Rock" });
	em.add<Position>(e, Position{ 0.0f, 0.0f });

	em.remove<Position>(e); // forces a migration back down; Name must be moved, not left dangling

	bool ok = true;
	em.view<Name>().for_each([&](Name& n) {
		ok = ok && n.value == "Rock";
	});

	return ok && !em.has<Position>(e);
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

// destroying a different entity from inside for_each() must not skip,
// double-visit, or read stale/destroyed memory for any entity, even the
// one swap-removed into the just-vacated row
static bool test_for_each_destroy_during_iteration_is_safe() {
	EntityManager em;
	Entity a = em.create(); em.add<Position>(a, Position{ 0.0f, 0.0f });
	Entity b = em.create(); em.add<Position>(b, Position{ 1.0f, 1.0f });
	Entity c = em.create(); em.add<Position>(c, Position{ 2.0f, 2.0f });
	Entity d = em.create(); em.add<Position>(d, Position{ 3.0f, 3.0f });

	std::vector<Entity> visited;
	em.view<Position>().for_each([&](Entity e, Position&) {
		visited.push_back(e);
		if (e == a) em.destroy(b); // non-last row; d gets swapped into b's old row mid-loop
	});

	bool visitedExactlyOnce = visited.size() == 3
		&& std::find(visited.begin(), visited.end(), a) != visited.end()
		&& std::find(visited.begin(), visited.end(), c) != visited.end()
		&& std::find(visited.begin(), visited.end(), d) != visited.end();

	auto pa = em.get<Position>(a);
	auto pc = em.get<Position>(c);
	auto pd = em.get<Position>(d);
	bool dataIntact = pa && pa->get().x == 0.0f && pc && pc->get().x == 2.0f && pd && pd->get().x == 3.0f;

	return !em.isAlive(b) && visitedExactlyOnce && dataIntact;
}

// adding a component to an entity from inside for_each() migrates it to a
// different archetype mid-loop; every entity must still be visited exactly
// once and no crash/OOB read should occur
static bool test_for_each_add_component_during_iteration_is_safe() {
	EntityManager em;
	Entity a = em.create(); em.add<Position>(a, Position{ 0.0f, 0.0f });
	Entity b = em.create(); em.add<Position>(b, Position{ 1.0f, 1.0f });
	Entity c = em.create(); em.add<Position>(c, Position{ 2.0f, 2.0f });

	int visited = 0;
	em.view<Position>().for_each([&](Entity e, Position&) {
		++visited;
		if (e == a) em.add<Velocity>(a, Velocity{ 9.0f, 9.0f }); // migrates a out of this archetype mid-loop
	});

	return visited == 3 && em.has<Velocity>(a) && em.has<Position>(b) && em.has<Position>(c);
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

// add<T>() on a non-last entity removes it from its old archetype's middle
// row, swapping the last row into that slot; verify the swapped-in entity's
// EntityTable row index gets patched, not just its raw storage
static bool test_add_middle_row_updates_swapped_entity() {
	EntityManager em;
	Entity a = em.create(); em.add<Position>(a, Position{ 1.0f, 1.0f });
	Entity b = em.create(); em.add<Position>(b, Position{ 2.0f, 2.0f });
	Entity c = em.create(); em.add<Position>(c, Position{ 3.0f, 3.0f }); // last row; gets swapped into a's old slot

	em.add<Velocity>(a, Velocity{ 9.0f, 9.0f }); // migrates a out; triggers the swap in the Position-only archetype

	auto pb = em.get<Position>(b);
	auto pc = em.get<Position>(c);

	return em.has<Velocity>(a) && !em.has<Velocity>(b) && !em.has<Velocity>(c)
		&& pb && pb->get().x == 2.0f && pb->get().y == 2.0f
		&& pc && pc->get().x == 3.0f && pc->get().y == 3.0f;
}

// remove<T>() on a non-last entity triggers the same middle-row swap as
// add<T>() above, but from the removal path
static bool test_remove_middle_row_updates_swapped_entity() {
	EntityManager em;
	Entity a = em.create(); em.add<Position>(a, Position{ 1.0f, 1.0f }); em.add<Velocity>(a, Velocity{ 1.0f, 1.0f });
	Entity b = em.create(); em.add<Position>(b, Position{ 2.0f, 2.0f }); em.add<Velocity>(b, Velocity{ 2.0f, 2.0f });
	Entity c = em.create(); em.add<Position>(c, Position{ 3.0f, 3.0f }); em.add<Velocity>(c, Velocity{ 3.0f, 3.0f }); // last row

	em.remove<Velocity>(a); // migrates a out of the Position+Velocity archetype; c swaps into a's old row

	auto pb = em.get<Position>(b);
	auto vb = em.get<Velocity>(b);
	auto pc = em.get<Position>(c);
	auto vc = em.get<Velocity>(c);

	return !em.has<Velocity>(a)
		&& pb && pb->get().x == 2.0f && vb && vb->get().vx == 2.0f
		&& pc && pc->get().x == 3.0f && vc && vc->get().vx == 3.0f;
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

// removeTarget's own cache-hit branch (from.removeEdge.find(...) hitting),
// as distinct from the addTarget cache-hit exercised above
static bool test_remove_edge_cache_hit_returns_same_archetype() {
	ArchetypeRegistry registry;
	Archetype& empty = registry.empty();
	ComponentId posId = ComponentType::get<Position>();

	Archetype& withPos = registry.addTarget(empty, posId, get_component_ops<Position>());

	Archetype& backToEmpty1 = registry.removeTarget(withPos, posId);
	Archetype& backToEmpty2 = registry.removeTarget(withPos, posId); // second call should hit the cache

	return &backToEmpty1 == &empty && &backToEmpty2 == &empty;
}

// Archetype::containsAll(): public but unused anywhere else in the
// codebase, so it has no incidental coverage from any other test
static bool test_archetype_contains_all() {
	ArchetypeRegistry registry;
	Archetype& empty = registry.empty();

	ComponentId posId = ComponentType::get<Position>();
	ComponentId velId = ComponentType::get<Velocity>();
	ComponentId radiusId = ComponentType::get<Radius>();

	Archetype& posVel = registry.addTarget(
		registry.addTarget(empty, posId, get_component_ops<Position>()),
		velId, get_component_ops<Velocity>());

	bool trueForEmptySet = posVel.containsAll({});
	bool trueForSubset = posVel.containsAll({ posId });
	bool trueForExactSet = posVel.containsAll({ posId, velId });
	bool falseWhenOneIdIsMissing = !posVel.containsAll({ posId, velId, radiusId });
	bool falseForUnrelatedId = !posVel.containsAll({ radiusId });

	return trueForEmptySet && trueForSubset && trueForExactSet
		&& falseWhenOneIdIsMissing && falseForUnrelatedId;
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

// a fully empty EntityManager (no entity ever created) must not crash,
// and both a typed and a zero-arity view must visit nothing
static bool test_empty_entity_manager_views_visit_nothing() {
	EntityManager em;

	int visitedTyped = 0;
	em.view<Position>().for_each([&](Position&) { ++visitedTyped; });

	int visitedAll = 0;
	em.view<>().for_each([&](Entity) { ++visitedAll; });

	return visitedTyped == 0 && visitedAll == 0 && em.size() == 0;
}

//------------------------------------------------------
// Query filtering: exclude<T...>
//------------------------------------------------------
static bool test_view_exclude_filters_matching_archetypes() {
	EntityManager em;
	Entity plain = em.create();
	em.add<Position>(plain, Position{ 0.0f, 0.0f });

	Entity frozen = em.create();
	em.add<Position>(frozen, Position{ 1.0f, 1.0f });
	em.add<Velocity>(frozen, Velocity{ 0.0f, 0.0f });

	std::vector<Entity> excludingVelocity;
	em.view<Position>().exclude<Velocity>().for_each([&](Entity e, Position&) {
		excludingVelocity.push_back(e);
	});

	bool excludesMatchingArchetype = excludingVelocity.size() == 1 && excludingVelocity[0] == plain;

	// excluding a component that's present nowhere in the manager is a no-op
	std::vector<Entity> excludingUnused;
	em.view<Position>().exclude<Health>().for_each([&](Entity e, Position&) {
		excludingUnused.push_back(e);
	});

	bool noopWhenExcludedComponentUnused = excludingUnused.size() == 2
		&& std::find(excludingUnused.begin(), excludingUnused.end(), plain) != excludingUnused.end()
		&& std::find(excludingUnused.begin(), excludingUnused.end(), frozen) != excludingUnused.end();

	return excludesMatchingArchetype && noopWhenExcludedComponentUnused;
}

//------------------------------------------------------
// Multi-archetype matrix: exercise several simultaneous
// archetypes at once, stressing columnIndexOf/matching
// logic beyond the 1-2 archetypes other tests juggle
//------------------------------------------------------
template <typename... Components>
static std::vector<Entity> collect_matching(EntityManager& em) {
	std::vector<Entity> result;
	em.view<Components...>().for_each([&](Entity e, Components&...) {
		result.push_back(e);
	});
	std::sort(result.begin(), result.end());
	return result;
}

static std::vector<Entity> sorted(std::vector<Entity> v) {
	std::sort(v.begin(), v.end());
	return v;
}

static bool test_multi_archetype_matrix_queries_return_expected_sets() {
	EntityManager em;

	Entity posOnly = em.create();
	em.add<Position>(posOnly, Position{ 0.0f, 0.0f });

	Entity posVel = em.create();
	em.add<Position>(posVel, Position{ 1.0f, 1.0f });
	em.add<Velocity>(posVel, Velocity{ 1.0f, 1.0f });

	Entity posRadius = em.create();
	em.add<Position>(posRadius, Position{ 2.0f, 2.0f });
	em.add<Radius>(posRadius, Radius{ 2.0f });

	Entity posVelRadius = em.create();
	em.add<Position>(posVelRadius, Position{ 3.0f, 3.0f });
	em.add<Velocity>(posVelRadius, Velocity{ 3.0f, 3.0f });
	em.add<Radius>(posVelRadius, Radius{ 3.0f });

	Entity healthOnly = em.create();
	em.add<Health>(healthOnly, Health{ 100.0f });

	Entity healthBounds = em.create();
	em.add<Health>(healthBounds, Health{ 50.0f });
	em.add<Bounds>(healthBounds, Bounds{ 10.0f, 20.0f });

	Entity posName = em.create();
	em.add<Position>(posName, Position{ 4.0f, 4.0f });
	em.add<Name>(posName, "Tagged");

	Entity velHealth = em.create();
	em.add<Velocity>(velHealth, Velocity{ 5.0f, 5.0f });
	em.add<Health>(velHealth, Health{ 25.0f });

	bool ok = true;

	ok = ok && collect_matching<Position>(em)
		== sorted({ posOnly, posVel, posRadius, posVelRadius, posName });

	ok = ok && collect_matching<Position, Velocity>(em)
		== sorted({ posVel, posVelRadius });

	ok = ok && collect_matching<Health>(em)
		== sorted({ healthOnly, healthBounds, velHealth });

	ok = ok && collect_matching<Velocity, Health>(em)
		== sorted({ velHealth });

	ok = ok && collect_matching<Bounds>(em)
		== sorted({ healthBounds });

	ok = ok && collect_matching<Name>(em)
		== sorted({ posName });

	ok = ok && collect_matching<Position, Velocity, Radius>(em)
		== sorted({ posVelRadius });

	return ok;
}

//------------------------------------------------------
// EntityManager::size()
//------------------------------------------------------
static bool test_size_tracks_live_entities_through_recycling() {
	EntityManager em;
	if (em.size() != 0) return false;

	Entity a = em.create();
	Entity b = em.create();
	if (em.size() != 2) return false;

	em.destroy(a);
	if (em.size() != 1) return false;

	Entity c = em.create(); // likely recycles a's index
	if (em.size() != 2) return false;

	em.destroy(b);
	em.destroy(c);
	return em.size() == 0;
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
	TESTEX("isAlive() is false for a never-allocated index", test_isAlive_false_for_out_of_range_index());
	TESTEX("generation keeps advancing across repeated recycle cycles", test_generation_bump_across_multiple_recycle_cycles());

	SUITE("Component add / remove / has");
	TESTEX("add() sets has() to true", test_add_sets_has());
	TESTEX("add() returns a reference to the stored value", test_add_returns_correct_value());
	TESTEX("remove() clears has()", test_remove_clears_has());
	TESTEX("archetype migration preserves earlier component data", test_archetype_migration_preserves_earlier_component());
	TESTEX("removing a component preserves the other components", test_remove_preserves_other_components());

	SUITE("add() forwarding construction");
	TESTEX("add() forwards a single constructor argument", test_add_forwards_single_constructor_arg());
	TESTEX("add() forwards multiple constructor arguments", test_add_forwards_multiple_constructor_args());

	SUITE("Component accessors: get() / has()");
	TESTEX("has() returns false for a component that was never added", test_has_returns_false_for_missing_component());
	TESTEX("get() returns the live value for an existing component", test_get_returns_value_for_existing_component());
	TESTEX("get() returns std::nullopt for a missing component", test_get_returns_nullopt_for_missing_component());
	TESTEX("const get() returns a value or std::nullopt correctly", test_get_const_overload_returns_value_and_nullopt());

	SUITE("Non-trivial component types");
	TESTEX("a std::string component survives archetype migration", test_non_trivial_component_round_trips());
	TESTEX("a std::string component survives remove()-triggered migration", test_remove_preserves_non_trivial_component());

	SUITE("view iteration");
	TESTEX("for_each visits exactly the matching entities", test_view_visits_all_matching_entities());
	TESTEX("entity-aware for_each visits the expected entities", test_view_entity_aware_overload_visits_expected_entities());
	TESTEX("destroying an entity from inside for_each is safe", test_for_each_destroy_during_iteration_is_safe());
	TESTEX("adding a component from inside for_each is safe", test_for_each_add_component_during_iteration_is_safe());

	SUITE("Removal / swap-remove semantics");
	TESTEX("destroying a middle entity preserves its siblings' data", test_destroy_preserves_sibling_component_data());
	TESTEX("add() on a middle-row entity patches the swapped-in entity's row", test_add_middle_row_updates_swapped_entity());
	TESTEX("remove() on a middle-row entity patches the swapped-in entity's row", test_remove_middle_row_updates_swapped_entity());

	SUITE("Archetype identity & signature caching");
	TESTEX("adding components in a different order interns to the same archetype", test_archetype_order_independent());
	TESTEX("addTarget/removeTarget caching is a true inverse", test_archetype_edge_caching_is_inverse());
	TESTEX("removeTarget's own cache-hit branch returns the same archetype", test_remove_edge_cache_hit_returns_same_archetype());
	TESTEX("containsAll() correctly checks subset/superset/unrelated id sets", test_archetype_contains_all());

	SUITE("Column growth");
	TESTEX("many entities survive multiple column growth cycles", test_many_entities_survive_column_growth());

	SUITE("Archetype transitions edge cases");
	TESTEX("removing the only component returns to the empty archetype", test_remove_only_component_returns_to_empty_archetype());
	TESTEX("destroying the last row entity in an archetype", test_destroy_last_row_entity());
	TESTEX("a recycled entity starts with no components", test_recycled_entity_starts_with_no_components());

	SUITE("view edge cases");
	TESTEX("view for an unused component visits nothing", test_view_for_unused_component_visits_nothing());
	TESTEX("zero-arity view visits every entity", test_zero_arity_view_visits_every_entity());
	TESTEX("a fully empty EntityManager's views visit nothing", test_empty_entity_manager_views_visit_nothing());

	SUITE("Query filtering: exclude<T...>");
	TESTEX("exclude<T>() filters out archetypes containing T", test_view_exclude_filters_matching_archetypes());

	SUITE("Multi-archetype matrix");
	TESTEX("queries return exactly the expected entity set across several archetypes", test_multi_archetype_matrix_queries_return_expected_sets());

	SUITE("EntityManager::size()");
	TESTEX("size() tracks live entities through create/destroy/recycling", test_size_tracks_live_entities_through_recycling());

	SUITE("Integration-style scenarios");
	TESTEX("movement system only moves matching entities", test_movement_system_only_moves_matching_entities());
	TESTEX("nested view iteration skips self and finds pairs", test_nested_view_iteration_skips_self_and_finds_pairs());
}
