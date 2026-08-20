#include <cstdint>
#include <vector>

using Entity = std::uint64_t;

#include "archetype.h"

using EntityIndex = std::uint32_t;
using EntityGeneration = std::uint32_t;

EntityIndex entityIndex(Entity entity)				{ return static_cast<EntityIndex>(entity & 0xFFFFFFFF); }
EntityGeneration entityGeneration(Entity entity)	{ return static_cast<EntityGeneration>((entity >> 32) & 0xFFFFFFFF); }

//---------------------------------------------------------------------------------------
// A record of entity data, including its generation, archetype, row index and alive status
//---------------------------------------------------------------------------------------
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
	Entity create(Archetype* archetype, std::size_t rowIndex) {
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
		entityDataTable[index] = { generation, archetype, rowIndex, true };

		return entity;
	}

	bool isAlive(Entity entity) {
		EntityIndex index = entityIndex(entity);
		if (index >= entityDataTable.size()) return false;
		return entityDataTable[index].isAlive && entityGeneration(entity) == entityDataTable[index].generation;
	}

	EntityData & getEntityData(Entity entity) {
		EntityIndex index = entityIndex(entity);
		return entityDataTable[index];
	}

	void destroyEntity(Entity entity) {
		auto& data = getEntityData(entity);
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

public:
    Entity createEntity() {
		auto empty = archetypeRegistry.empty();		// initial archetype for new entities
		Entity e = entityTable.create(&empty, 0);	// create a new entity in the entity table
		std::size_t row = empty.addEntity(e);		// add the entity to the empty archetype


		return e;
	}

	void destroyEntity(Entity entity) {
		/* Implementation */ 
	}

	bool isEntityAlive(Entity entity) { return entityTable.isAlive(entity); }

	// add a component to an entity and return a reference to it
	template<typename ComponentType>
	ComponentType& addComponent(Entity entity, ComponentType component = ComponentType()) { /* Implementation */ }

	// get a reference to a component of an entity
	template<typename ComponentType>
	ComponentType& getComponent(Entity entity) { /* Implementation */ }
};

