#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <array>
#include <optional>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

// Enforces an assert unconditionally, independent of NDEBUG. Violating this assertion
// is a calling contract violation not a runtime error to handle and recover. So
// the only thing this needs to do is provide a debuggable abort instead of letting
// a stripped-out assert() let the violation through as undefined behavior in a Release build.
#define ENTITY_ASSERT(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "entity: assertion violated: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
			std::abort(); \
		} \
	} while (0)

using Entity = std::uint64_t;

// A null entity handle, representing an invalid or non-existent entity. Deliberately all bits
// set (generation AND index at max), not just index at max: a real entity handle would need
// that one specific index to also be individually destroyed-and-recycled ~2^32 times before it
// could ever collide with this value, vs. just needing 2^32 entities alive at once.
constexpr Entity NullEntity = std::numeric_limits<Entity>::max();

#include "component_props.h"
#include "archetype.h"

using EntityIndex		= std::uint32_t;
using EntityGeneration	= std::uint32_t;

inline EntityIndex entityIndex(Entity entity) noexcept				{ return static_cast<EntityIndex>(entity & 0xFFFFFFFF); }
inline EntityGeneration entityGeneration(Entity entity) noexcept	{ return static_cast<EntityGeneration>(entity >> 32); }
inline Entity makeEntity(EntityIndex index, EntityGeneration generation) noexcept { return (static_cast<Entity>(generation) << 32) | index; }

//-----------------------------------------------------------------------------------------
// A record of entity data, including its generation, archetype, row index and alive status
//-----------------------------------------------------------------------------------------
struct EntityData
{
	EntityGeneration generation = 0;
	Archetype* archetype = nullptr;
	std::size_t rowIndex = 0;
	bool isAlive = false;
};

//----------------------------------------------------------------------------------
// The EntityTable class manages the storage and retrieval of entity data
//----------------------------------------------------------------------------------
class EntityTable
{
private:
	std::vector<EntityData> entityDataTable;	// table of entity data, indexed by entity index
	std::vector<EntityIndex> freeEntityList;	// list of free entity indices for reuse
	std::size_t liveCount = 0;					// number of currently alive entities

public:
	// create an entity with the given archetype and add it to the table
	Entity create(Archetype* archetype, std::size_t row) {
		EntityIndex index;

		// Reuse an index from the free list if available, otherwise create a new index
		if (!freeEntityList.empty()) {
			index = freeEntityList.back();
			freeEntityList.pop_back();
		} else {
			index = static_cast<EntityIndex>(entityDataTable.size());
			entityDataTable.emplace_back();
		}

		EntityGeneration generation = entityDataTable[index].generation;
		Entity entity = makeEntity(index, generation);

		entityDataTable[index] = { generation, archetype, row, true };
		++liveCount;

		return entity;
	}

	// number of currently alive entities
	std::size_t size() const noexcept { return liveCount; }

	// reserve capacity for at least `n` entities, to avoid vector growth when bulk-creating
	void reserve(std::size_t n) { entityDataTable.reserve(n); }

	bool isAlive(Entity entity) const noexcept {
		EntityIndex index = entityIndex(entity);	// get the index from the entity handle

		// check bounds
		if (index >= entityDataTable.size()) 
			return false;

		// check that object is alive and that generation matches
		return entityDataTable[index].isAlive && entityGeneration(entity) == entityDataTable[index].generation;
	}

	// const-qualified for a const EntityTable, non-const otherwise (deduced from self)
	template <typename Self>
	auto& record(this Self& self, Entity entity) noexcept {
		ENTITY_ASSERT(self.isAlive(entity));	// ensure the entity is alive before accessing its record

		EntityIndex index = entityIndex(entity);
		return self.entityDataTable[index];
	}

	// non-asserting counterpart of record(): returns nullptr instead of failing a precondition
	// when the entity is dead or out of range, doing the liveness check exactly once
	EntityData* tryRecord(Entity entity) noexcept {
		EntityIndex index = entityIndex(entity);

		if (index >= entityDataTable.size())
			return nullptr;

		EntityData& data = entityDataTable[index];
		if (!data.isAlive || entityGeneration(entity) != data.generation)
			return nullptr;

		return &data;
	}

	void setLocation(Entity entity, Archetype* archetype, std::size_t row) noexcept {
		EntityIndex index = entityIndex(entity);
		entityDataTable[index].archetype = archetype;
		entityDataTable[index].rowIndex = row;
	}

	// Update the row index of an entity in the entity data table
	void updateRow(Entity entity, std::size_t newRow) noexcept {
		record(entity).rowIndex = newRow;
	}

	// Destroy an entity by marking it as dead and incrementing its generation
	void destroy(Entity entity) {
		auto& data = record(entity);
		data.isAlive = false;
		data.generation++;
		freeEntityList.push_back(entityIndex(entity));
		--liveCount;
	}
};

//----------------------------------------------------------------------------------
// EntityView class allows iteration over entities with specific component types
//----------------------------------------------------------------------------------
template <typename... Components>
class EntityView {

private:
	ArchetypeRegistry* registry;
	EntityTable* entityTable;
	std::vector<ComponentId> excluded;

	// Overload: lambda accepts (Components&...)
	template <typename Func, std::size_t N, std::size_t... I>
	static auto invoke(Func& func, Archetype& archetype, const std::array<int, N>& column_index,
		std::size_t row, std::index_sequence<I...>)
		-> decltype(func(*static_cast<Components*>(nullptr)...), void())
	{
		func(*static_cast<Components*>(archetype.column(column_index[I]).at(row))...);
	}

	// Overload: lambda accepts (Entity, Components&...)
	template<typename Func, std::size_t N, std::size_t... I>
	static auto invoke(Func& func, Archetype& archetype, const std::array<int, N>& column_index,
		std::size_t row, std::index_sequence<I...>)
		-> decltype(func(std::declval<Entity>(), *static_cast<Components*>(nullptr)...), void())
	{
		func(archetype.entityAt(row), *static_cast<Components*>(archetype.column(column_index[I]).at(row))...);
	}

public:
	explicit EntityView(ArchetypeRegistry& registry, EntityTable& entityTable) noexcept
		: registry(&registry), entityTable(&entityTable) {}

	// exclude entities whose archetype contains any of the given component types
	template <typename... Excluded>
	EntityView& exclude() {
		(excluded.push_back(ComponentType::get<Excluded>()), ...);
		return *this;
	}

	// Iterate over all entities with the specified component types (and none of the excluded
	// types) and invoke the provided function. Safe to add/remove/destroy entities from within
	// func: entities destroyed or migrated out of the archetype by an earlier callback in this
	// same for_each are skipped rather than read after they're invalid; entities created during
	// iteration are not visited.
	template <typename Func>
	void for_each(Func&& func) {
		constexpr std::size_t n = sizeof...(Components);
		std::array<ComponentId, n> wanted{ ComponentType::get<Components>()... };

		// capture the archetype count up front so interning a new archetype mid-callback (which
		// can reallocate registry->all()'s backing vector) can't invalidate this iteration;
		// registry->all() only ever appends, so indexing by position stays valid across a
		// reallocation and a newly-appended archetype beyond this count is simply not visited
		std::size_t archetypeCount = registry->all().size();

		for (std::size_t ai = 0; ai < archetypeCount; ++ai) {
			Archetype& archetype = *registry->all()[ai];
			std::array<int, n> column_index{};
			bool matches = true;

			for (std::size_t i = 0; i < n; ++i) {
				int idx = archetype.columnIndexOf(wanted[i]);

				if (idx < 0) {
					matches = false;
					break;
				}

				column_index[i] = idx;
			}

			if (matches) {
				for (ComponentId excluded_id : excluded) {
					if (archetype.contains(excluded_id)) {
						matches = false;
						break;
					}
				}
			}

			if (!matches)
				continue;

			// snapshot this archetype's entities before invoking any callback for it, since
			// func may destroy/add/remove components and swap-remove rows out from under us
			std::vector<Entity> row_entities = archetype.entityList();

			for (Entity e : row_entities) {
				EntityData* rec = entityTable->tryRecord(e);

				if (!rec || rec->archetype != &archetype)
					continue;	// destroyed, or migrated to a different archetype, by an earlier callback in this loop

				invoke(func, archetype, column_index, rec->rowIndex, std::index_sequence_for<Components...>{});
			}
		}
	}
};

//----------------------------------------------------------------------------------
// EntityManager class manages the creation, lifetime and destruction of entities
//----------------------------------------------------------------------------------
class EntityManager
{
private:
	EntityTable entityTable;				// table of entity data, indexed by entity index
	ArchetypeRegistry archetypeRegistry;	// registry of archetypes, indexed by archetype id

	// per-instance cache from ComponentSetType::get<Components...>() (a distinct id per
	// create<Components...>() instantiation) to the archetype it resolves to, so repeated
	// spawns of the same shape after the first skip rebuilding/re-hashing the signature; see
	// create<Components...>() below
	std::vector<Archetype*> createShapeCache;

	// placement-constructs each of `components` into its column at `row`, using `ids[I]`
	// (in original, pre-sort pack order) to find that component's column
	template <std::size_t N, std::size_t... I, typename... Components>
	static void constructComponents(Archetype& archetype, std::size_t row, const std::array<ComponentId, N>& ids,
		std::index_sequence<I...>, Components&&... components) {
		(new (archetype.column(archetype.columnIndexOf(ids[I])).at(row)) std::decay_t<Components>(std::forward<Components>(components)), ...);
	}

public:
    Entity create() {
		Archetype &empty = archetypeRegistry.empty();		// initial archetype for new entities
		Entity e = entityTable.create(&empty, 0);			// create a new entity in the entity table
		auto row = empty.pushUninitializedRow(e);	// add the entity to the empty archetype
		entityTable.setLocation(e, &empty, row);			// set the entity's location in the entity table

		return e;
	}

	// Reserve capacity for at least `n` entities, to avoid repeated vector growth when
	// bulk-creating (e.g. spawning a level's worth of entities up front). Every new entity
	// starts in the empty archetype, so this only needs to cover the entity table and that
	// archetype's entity list -- components added afterward still grow their own archetype's
	// storage on demand.
	void reserve(std::size_t n) {
		entityTable.reserve(n);
		archetypeRegistry.empty().reserve(n);
	}

	// Create an entity with the given components already attached, constructing each one in
	// place in the entity's final archetype. Unlike create() followed by one add<T>() per
	// component, this never visits an intermediate archetype: each already-placed component
	// would otherwise be move-constructed again on every subsequent add<T>() call, and this
	// skips all of that by resolving the final archetype up front.
	template <typename... Components>
	Entity create(Components&&... components) {
		constexpr std::size_t n = sizeof...(Components);
		std::array<ComponentId, n> ids{ ComponentType::get<std::decay_t<Components>>()... };

		ComponentId shapeId = ComponentSetType::get<std::decay_t<Components>...>();
		if (shapeId >= createShapeCache.size())
			createShapeCache.resize(shapeId + 1, nullptr);

		Archetype*& cached = createShapeCache[shapeId];
		if (!cached) {
			std::array<const ComponentOps*, n> ops{ get_component_ops<std::decay_t<Components>>()... };

			// canonicalize into sorted signature order, same convention archetypes are interned by
			std::array<std::size_t, n> order{};
			for (std::size_t i = 0; i < n; ++i) order[i] = i;
			std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return ids[a] < ids[b]; });

			Signature sig(n);
			std::vector<const ComponentOps*> sortedOps(n);
			for (std::size_t i = 0; i < n; ++i) {
				sig[i] = ids[order[i]];
				sortedOps[i] = ops[order[i]];
			}

			// forSignature() asserts sig is duplicate-free, which also catches a repeated
			// component type in Components... here
			cached = &archetypeRegistry.forSignature(sig, sortedOps);
		}

		Archetype& archetype = *cached;

		Entity e = entityTable.create(&archetype, 0);
		auto row = archetype.pushUninitializedRow(e);
		entityTable.setLocation(e, &archetype, row);

		constructComponents(archetype, row, ids, std::index_sequence_for<Components...>{}, std::forward<Components>(components)...);

		return e;
	}

	// Destroy an entity by removing it from its archetype and marking it as dead in the entity table
	void destroy(Entity entity) {
		ENTITY_ASSERT(isAlive(entity));

		EntityData& record = entityTable.record(entity);
		Entity moved = record.archetype->removeRow(record.rowIndex);

		if (moved != NullEntity) {
			entityTable.updateRow(moved, record.rowIndex);
		}

		entityTable.destroy(entity);
	}

	bool isAlive(Entity entity) const noexcept { return entityTable.isAlive(entity); }

	// number of currently alive entities
	std::size_t size() const noexcept { return entityTable.size(); }

	// add a component of type T to an entity, moving it to a new archetype if necessary
	template <typename T, typename... Args>
	T& add(Entity e, Args&&... args) {
		ENTITY_ASSERT(isAlive(e));

		EntityData& record = entityTable.record(e);
		Archetype& current_archetype = *record.archetype;
		ComponentId added_component_id = ComponentType::get<T>();

		ENTITY_ASSERT(!current_archetype.contains(added_component_id) && "component already present on entity");

		Archetype& new_archetype = archetypeRegistry.addTarget(current_archetype, added_component_id, get_component_ops<T>());
		auto old_row = record.rowIndex;
		auto new_row = new_archetype.pushUninitializedRow(e);

		for (ComponentId id : current_archetype.type_ids()) {
			int src_idx = current_archetype.columnIndexOf(id);
			int dst_idx = new_archetype.columnIndexOf(id);
			current_archetype.column(src_idx).getOps()->moveConstruct(new_archetype.column(dst_idx).at(new_row), current_archetype.column(src_idx).at(old_row));
		}

		int new_col_idx = new_archetype.columnIndexOf(added_component_id);
		void* slot = new_archetype.column(new_col_idx).at(new_row);
		T* value = new (slot) T(std::forward<Args>(args)...);

		Entity moved = current_archetype.finishRemovingRow(old_row);
		if (moved != NullEntity) 
			entityTable.updateRow(moved, old_row);

		entityTable.setLocation(e, &new_archetype, new_row);

		return *value;
	}

	// remove a component of type T from an entity
	template <typename T>
	void remove(Entity e) {
		ENTITY_ASSERT(isAlive(e));
		
		EntityData& record = entityTable.record(e);
		Archetype& current_archetype = *record.archetype;
		ComponentId removed_id = ComponentType::get<T>();
		
		ENTITY_ASSERT(current_archetype.contains(removed_id) && "component not present on entity");

		Archetype& new_archetype = archetypeRegistry.removeTarget(current_archetype, removed_id);
		auto old_row = record.rowIndex;
		auto new_row = new_archetype.pushUninitializedRow(e);

		for (ComponentId id : new_archetype.type_ids()) {
			int src_idx = current_archetype.columnIndexOf(id);
			int dst_idx = new_archetype.columnIndexOf(id);
			current_archetype.column(src_idx).getOps()->moveConstruct(new_archetype.column(dst_idx).at(new_row), current_archetype.column(src_idx).at(old_row));
		}

		current_archetype.column(current_archetype.columnIndexOf(removed_id)).destroyAt(old_row);
		Entity moved = current_archetype.finishRemovingRow(old_row);

		if (moved != NullEntity) 
			entityTable.updateRow(moved, old_row);

		entityTable.setLocation(e, &new_archetype, new_row);
	}

	// returns the component if the entity has it, or std::nullopt otherwise;
	// a single deducing-this template covers both the const and non-const case
	template <typename T, typename Self>
	std::optional<std::reference_wrapper<std::conditional_t<std::is_const_v<Self>, const T, T>>>
	get(this Self& self, Entity e) noexcept {
		ENTITY_ASSERT(self.isAlive(e));

		auto& rec = self.entityTable.record(e);
		int idx = rec.archetype->columnIndexOf(ComponentType::get<T>());

		using ComponentT = std::conditional_t<std::is_const_v<Self>, const T, T>;

		if (idx < 0)
			return std::nullopt;

		return std::ref(*static_cast<ComponentT*>(rec.archetype->column(idx).at(rec.rowIndex)));
	}

	// return true if entity has a given component
	template <typename T>
	bool has(Entity e) noexcept {
		ENTITY_ASSERT(isAlive(e));

		const EntityData& rec = entityTable.record(e);
		return rec.archetype->contains(ComponentType::get<T>());
	}

	// get an iterable view of entities having the requested set of components
	template <typename... Components>
	EntityView<Components...> view() noexcept {
		return EntityView<Components...>(archetypeRegistry, entityTable);
	}
};
