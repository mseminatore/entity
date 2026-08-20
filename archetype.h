//
// storage for a single component type, which is a vector of component data for each entity in the archetype
//
class Column
{
private:
public:
};

using ComponentId = std::size_t;	// unique identifier for a component type

//
// storage for a collection of columns, representing a unique combination of component types
//
class Archetype
{
private:
	std::vector<ComponentId> componentTypes;	// unique combination of component types for this archetype
	std::vector<Column> columns;				// storage for each component type in the archetype
	std::vector<Entity> entities;				// list of entities in this archetype

public:
	std::size_t addEntity(Entity entity) {
		// add a new entity to the archetype and return its row index
		for (ComponentId componentType : componentTypes) {
			// add a new row to each column for the new entity
			columns[componentType].addRow();
		}

		entities.push_back(entity);
		return entities.size() - 1;
	}
};

//
//
//
class ArchetypeRegistry
{
private:
	Archetype *emptyArchetype = nullptr;	// archetype with no components, used for new entities

public:
	Archetype& empty() { return *emptyArchetype; }
};
