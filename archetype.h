#include <cstddef>

//-----------------------------------------------------------------------------------------------------------
// storage for a single component type, which is a vector of component data for each entity in the archetype
//-----------------------------------------------------------------------------------------------------------
class Column
{
private:
	std::byte* data = nullptr;
	std::size_t count = 0;
	std::size_t capacity = 0;

	// grow storage by doubling
	void grow() {
		std::size_t new_size = capacity == 0 ? 4 : capacity * 2;
		auto *new_data = 

		for (auto i = 0; i < count; i++) {
			move_construct(new_data + i * )
		}

		release_buffer();
		data = new_data;
		capacity = new_size;
	}

	// release the memory block
	void release_buffer() {
		if (data)
			::operator delete(data);
	}

	// destroy each object before releasing the memory block
	void release() {
		if (data) {
			for (auto i = 0; i < count; i++) {
				destroy(at(i));
				release_buffer();
			}
		}
	}

public:
	virtual ~Column() { release(); }
	std::size_t count() { return count; }
	
	void* at(std::size_t row) { return data + row * size; }

	void* push_uninitialized() {
		// grow if we are full
		if (count == capacity)
			grow();

		return data + (count++) * size;
	}

	void destroy_at() {

	}

	void move_last_into() {

	}
};

using ComponentId = std::size_t;	// unique identifier for a component type

//--------------------------------------------------------------------------------------------
// storage for a collection of columns, representing a unique combination of component types
//--------------------------------------------------------------------------------------------
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

//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
class ArchetypeRegistry
{
private:
	Archetype *emptyArchetype = nullptr;	// archetype with no components, used for new entities

public:
	Archetype& empty() { return *emptyArchetype; }
};
