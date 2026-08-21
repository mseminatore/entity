#include <cstddef>
#include <new>
#include <unordered_map>

//-----------------------------------------------------------------------------------------------------------
// storage for a single component type, which is a vector of component data for each entity in the archetype
//-----------------------------------------------------------------------------------------------------------
class Column
{
private:
	std::byte* data = nullptr;
	std::size_t count = 0;
	std::size_t capacity = 0;
	const ComponentOps* ops = nullptr;

	// grow storage by doubling
	void grow() {
		std::size_t new_capacity = capacity == 0 ? 4 : capacity * 2;
		auto* new_data = static_cast<std::byte*>(::operator new(new_capacity * ops->size, std::align_val_t{ ops->alignment }));

		for (auto i = 0; i < count; i++) {
			ops->moveConstruct(new_data + i * ops->size, data + i * ops->size);
		}

		releaseBuffer();
		data = new_data;
		capacity = new_capacity;
	}

	// release the memory block
	void releaseBuffer() {
		if (data)
			::operator delete(data, std::align_val_t{ ops->alignment });
	}

	// destroy each object before releasing the memory block
	void release() {
		if (data) {
			for (auto i = 0; i < count; i++) {
				ops->destroy(at(i));
				releaseBuffer();
			}
		}
	}

public:
	Column(const ComponentOps* ops) : ops(ops) {}

	virtual ~Column() { release(); }
	std::size_t size() const { return count; }
	
	void* at(std::size_t row) { return data + row * ops->size; }

	void* pushUninitialized() {
		// grow if we are full
		if (count == capacity)
			grow();

		return data + (count++) * ops->size;
	}

	void destroyAt(std::size_t row) {
		ops->destroy(at(row));
	}

	void moveLastInto(std::size_t row) {
		std::size_t last_row = count - 1;

		if (last_row != row) {
			// move the last row into the specified row
			ops->moveConstruct(at(row), at(last_row));
		}
		count--;
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

	std::unordered_map<ComponentId, Archetype*> addEdge;
	std::unordered_map<ComponentId, Archetype*> removeEdge;

public:

	Archetype(std::vector<ComponentId> ids, const std::vector<const ComponentOps*>& ops) {
		componentTypes = std::move(ids);
		
		// ensure that the columns vector has enough space for the component type
		columns.reserve(ops.size());

		for (const ComponentOps* op : ops) {
			columns.emplace_back(op);
		}
	}

	std::size_t size() const { return entities.size(); }

	int columnIndexOf(ComponentId id) const {
		for (auto i = 0; i < componentTypes.size(); ++i) {
			if (componentTypes[i] == id) {
				return i;
			}
		}

		return -1; // not found
	}

	bool contains(ComponentId id) const { return std::find(componentTypes.begin(), componentTypes.end(), id) != componentTypes.end(); }

	bool containsAll(const std::vector<ComponentId>& ids) const {
		for (ComponentId id : ids) {
			if (!contains(id)) {
				return false;
			}
		}
		return true;
	}

	std::size_t columnCount() const { return columns.size(); }

	std::size_t pushUninitializedRow(Entity entity) {

		// add a new entity with empty components to the archetype and return its row index
		for (auto &col : columns) {
			// add a new row to each column for the new entity
			col.pushUninitialized();
		}

		entities.push_back(entity);
		return entities.size() - 1;
	}

	// destroy the components of the entity at the specified row and remove it from the archetype
	Entity removeRow(std::size_t row) {
		for (auto &col : columns) {
			col.destroyAt(row);
		}

		return finishRemovingRow(row);
	}

	// move the last entity into the specified row and return the moved entity, or NullEntity if the removed row was the last row
	Entity finishRemovingRow(std::size_t row) {
		std::size_t last = entities.size() - 1;
		for (auto& col : columns) col.moveLastInto(row);
		Entity moved = NullEntity;

		if (row != last) {
			entities[row] = entities[last];
			moved = entities[row];
		}

		entities.pop_back();
		return moved;
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
