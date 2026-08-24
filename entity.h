#include <assert.h>
#include <cstdint>
#include <vector>
#include <array>
#include <optional>
#include <functional>
#include <type_traits>

using Entity = std::uint64_t;
constexpr Entity NullEntity = 0xFFFFFFFF;	// a null entity handle, representing an invalid or non-existent entity

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
		Entity entity = (static_cast<Entity>(generation) << 32) | index;

		entityDataTable[index] = { generation, archetype, row, true };
		++liveCount;

		return entity;
	}

	// number of currently alive entities
	std::size_t size() const noexcept { return liveCount; }

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
		assert(self.isAlive(entity));	// ensure the entity is alive before accessing its record

		EntityIndex index = entityIndex(entity);
		return self.entityDataTable[index];
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
public:
	explicit EntityView(ArchetypeRegistry& registry) noexcept : registry(&registry) {}

	// Iterate over all entities with the specified component types and invoke the provided function
	template <typename Func>
	void for_each(Func&& func) {
		constexpr std::size_t n = sizeof...(Components);
		std::array<ComponentId, n> wanted{ ComponentType::get<Components>()... };

		for (const auto& archetype_ptr : registry->all()) {
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

			if (!matches) 
				continue;

			auto count = archetype.size();
			for (std::size_t row = 0; row < count; row++) {
				invoke(func, archetype, column_index, row, std::index_sequence_for<Components...>{});
			}
		}
	}

private:

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

	ArchetypeRegistry* registry;
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
		auto row = empty.pushUninitializedRow(e);	// add the entity to the empty archetype
		entityTable.setLocation(e, &empty, row);			// set the entity's location in the entity table

		return e;
	}

	// Destroy an entity by removing it from its archetype and marking it as dead in the entity table
	void destroy(Entity entity) {
		assert(isAlive(entity));

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
		assert(isAlive(e));

		EntityData& record = entityTable.record(e);
		Archetype& current_archetype = *record.archetype;
		ComponentId added_component_id = ComponentType::get<T>();

		assert(!current_archetype.contains(added_component_id) && "component already present on entity");

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
		assert(isAlive(e));
		
		EntityData& record = entityTable.record(e);
		Archetype& current_archetype = *record.archetype;
		ComponentId removed_id = ComponentType::get<T>();
		
		assert(current_archetype.contains(removed_id) && "component not present on entity");

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
		assert(self.isAlive(e));

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
		assert(isAlive(e));

		const EntityData& rec = entityTable.record(e);
		return rec.archetype->contains(ComponentType::get<T>());
	}

	// get an iterable view of entities having the requested set of components
	template <typename... Components>
	EntityView<Components...> view() noexcept {
		return EntityView<Components...>(archetypeRegistry);
	}
};

