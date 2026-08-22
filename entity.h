#include <assert.h>
#include <cstdint>
#include <vector>
#include <array>

using Entity = std::uint64_t;
constexpr Entity NullEntity = 0xFFFFFFFF;	// a null entity handle, representing an invalid or non-existent entity

#include "component_props.h"
#include "archetype.h"

using EntityIndex = std::uint32_t;
using EntityGeneration = std::uint32_t;

inline EntityIndex entityIndex(Entity entity)				{ return static_cast<EntityIndex>(entity & 0xFFFFFFFF); }
inline EntityGeneration entityGeneration(Entity entity)	{ return static_cast<EntityGeneration>(entity >> 32); }

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

public:
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
		Entity entity = (static_cast<Entity>(generation) << 32) | index;
		entityDataTable[index] = { generation, archetype, row, true };

		return entity;
	}

	bool isAlive(Entity entity) {
		EntityIndex index = entityIndex(entity);	// get the index from the entity handle

		// check bounds
		if (index >= entityDataTable.size()) 
			return false;

		// check that object is alive and that generation matches
		return entityDataTable[index].isAlive && entityGeneration(entity) == entityDataTable[index].generation;
	}

	EntityData& record(Entity entity) {
		assert(isAlive(entity));	// ensure the entity is alive before accessing its record

		EntityIndex index = entityIndex(entity);
		return entityDataTable[index];
	}

	void setLocation(Entity entity, Archetype* archetype, std::size_t row) {
		EntityIndex index = entityIndex(entity);
		entityDataTable[index].archetype = archetype;
		entityDataTable[index].rowIndex = row;
	}

	// Update the row index of an entity in the entity data table
	void updateRow(Entity entity, std::size_t newRow) {
		record(entity).rowIndex = newRow;
	}

	// Destroy an entity by marking it as dead and incrementing its generation
	void destroy(Entity entity) {
		auto& data = record(entity);
		data.isAlive = false;
		data.generation++;
		freeEntityList.push_back(entityIndex(entity));
	}
};

//----------------------------------------------------------------------------------
// Query class allows iteration over entities with specific component types
//----------------------------------------------------------------------------------
template <typename... Components>
class Query {
public:
	explicit Query(ArchetypeRegistry& registry) : registry_(&registry) {}

	template <typename Func>
	void for_each(Func&& func) {
		constexpr std::size_t n = sizeof...(Components);
		std::array<ComponentId, n> wanted{ ComponentType::get<Components>()... };

		for (const auto& archetype_ptr : registry_->all()) {
			Archetype& archetype = *archetype_ptr;
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
			if (!matches) continue;

			std::size_t count = archetype.size();
			for (std::size_t row = 0; row < count; ++row) {
				invoke(func, archetype, column_index, row, std::index_sequence_for<Components...>{});
			}
		}
	}

private:
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

	ArchetypeRegistry* registry_;
};

//----------------------------------------------------------------------------------
// EntityManager class manages the creation, lifetime and destruction of entities
//----------------------------------------------------------------------------------
class EntityManager
{
private:
	EntityTable entityTable;				// table of entity data, indexed by entity index
	ArchetypeRegistry archetypeRegistry;	// registry of archetypes, indexed by archetype id

public:
    Entity create() {
		Archetype &empty = archetypeRegistry.empty();		// initial archetype for new entities
		Entity e = entityTable.create(&empty, 0);			// create a new entity in the entity table
		std::size_t row = empty.pushUninitializedRow(e);	// add the entity to the empty archetype
		entityTable.setLocation(e, &empty, row);			// set the entity's location in the entity table

		return e;
	}

	// Destroy an entity by removing it from its archetype and marking it as dead in the entity table
	void destroy(Entity entity) {
		assert(isEntityAlive(entity));

		EntityData& record = entityTable.record(entity);
		Entity moved = record.archetype->removeRow(record.rowIndex);

		if (moved != NullEntity) {
			entityTable.updateRow(moved, record.rowIndex);
		}

		entityTable.destroy(entity);
	}

	bool isEntityAlive(Entity entity) { return entityTable.isAlive(entity); }

	// add a component of type T to an entity, moving it to a new archetype if necessary
	template <typename T, typename... Args>
	T& add(Entity e, Args&&... args) {
		assert(isEntityAlive(e));

		EntityData& record = entityTable.record(e);
		Archetype& from = *record.archetype;
		ComponentId added_id = ComponentType::get<T>();

		assert(!from.contains(added_id) && "component already present on entity");

		Archetype& to = archetypeRegistry.addTarget(from, added_id, get_component_ops<T>());
		std::size_t old_row = record.rowIndex;
		std::size_t new_row = to.pushUninitializedRow(e);

		for (ComponentId id : from.type_ids()) {
			int src_idx = from.columnIndexOf(id);
			int dst_idx = to.columnIndexOf(id);
			from.column(src_idx).getOps()->moveConstruct(to.column(dst_idx).at(new_row), from.column(src_idx).at(old_row));
		}

		int new_col_idx = to.columnIndexOf(added_id);
		void* slot = to.column(new_col_idx).at(new_row);
		T* value = new (slot) T(std::forward<Args>(args)...);

		Entity moved = from.finishRemovingRow(old_row);
		if (moved != NullEntity) 
			entityTable.updateRow(moved, old_row);

		entityTable.setLocation(e, &to, new_row);

		return *value;
	}

	template <typename T>
	void remove(Entity e) {
		assert(isEntityAlive(e));
		
		EntityData& record = entityTable.record(e);
		Archetype& from = *record.archetype;
		ComponentId removed_id = ComponentType::get<T>();
		
		assert(from.contains(removed_id) && "component not present on entity");

		Archetype& to = archetypeRegistry.removeTarget(from, removed_id);
		std::size_t old_row = record.rowIndex;
		std::size_t new_row = to.pushUninitializedRow(e);

		for (ComponentId id : to.type_ids()) {
			int src_idx = from.columnIndexOf(id);
			int dst_idx = to.columnIndexOf(id);
			from.column(src_idx).getOps()->moveConstruct(to.column(dst_idx).at(new_row), from.column(src_idx).at(old_row));
		}

		from.column(from.columnIndexOf(removed_id)).destroyAt(old_row);
		Entity moved = from.finishRemovingRow(old_row);

		if (moved != NullEntity) 
			entityTable.updateRow(moved, old_row);

		entityTable.setLocation(e, &to, new_row);
	}

	template <typename T>
	bool has(Entity e) {
		assert(isEntityAlive(e));

		const EntityData& rec = entityTable.record(e);
		return rec.archetype->contains(ComponentType::get<T>());
	}

	template <typename... Components>
	Query<Components...> query() {
		return Query<Components...>(archetypeRegistry);
	}
};

