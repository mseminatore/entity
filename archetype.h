#pragma once

#include <cstddef>
#include <new>
#include <unordered_map>
#include <memory>

//-----------------------------------------------------------------------------------------------------------
// storage for a single component type, which is a vector of component data for each entity in the archetype
//-----------------------------------------------------------------------------------------------------------
class Column
{
private:
	constexpr static std::size_t MinCapacity = 4;	// minimum capacity for a column, to avoid frequent reallocations
	constexpr static std::size_t GrowthFactor = 2;	// factor by which to grow the column capacity when needed

	std::byte* data = nullptr;
	std::size_t count = 0;
	std::size_t capacity = 0;
	const ComponentOps* ops = nullptr;

	// grow storage by doubling
	void grow() {
		std::size_t new_capacity = capacity == 0 ? MinCapacity : capacity * GrowthFactor;
		auto* new_data = static_cast<std::byte*>(::operator new(new_capacity * ops->size, std::align_val_t{ ops->alignment }));

		for (std::size_t i = 0; i < count; i++) {
			ops->moveConstruct(new_data + i * ops->size, data + i * ops->size);
		}

		releaseBuffer();
		data = new_data;
		capacity = new_capacity;
	}

	// release the memory block
	void releaseBuffer() noexcept {
		if (data)
			::operator delete(data, std::align_val_t{ ops->alignment });
	}

	// destroy each object before releasing the memory block. Assumes component
	// destructors do not throw, matching the standard convention for destructors.
	void release() noexcept {
		if (data) {
			for (std::size_t i = 0; i < count; i++) {
				ops->destroy(at(i));
			}
			releaseBuffer();
			data = nullptr;
			count = 0;
			capacity = 0;
		}
	}

public:
	Column(const ComponentOps* ops) noexcept : ops(ops) {}

	Column(const Column&) = delete;
	Column& operator=(const Column&) = delete;

	Column(Column&& other) noexcept
		: data(other.data), count(other.count), capacity(other.capacity), ops(other.ops) {
		other.data = nullptr;
		other.count = 0;
		other.capacity = 0;
	}

	Column& operator=(Column&& other) noexcept {
		if (this != &other) {
			release();
			data = other.data;
			count = other.count;
			capacity = other.capacity;
			ops = other.ops;
			other.data = nullptr;
			other.count = 0;
			other.capacity = 0;
		}
		return *this;
	}

	~Column() { release(); }

	std::size_t size() const noexcept { return count; }
	const ComponentOps* getOps() const noexcept { return ops; }

	void* at(std::size_t row) noexcept { return data + row * ops->size; }

	void* pushUninitialized() {
		// grow if we are full
		if (count == capacity)
			grow();

		return data + (count++) * ops->size;
	}

	// assumes the component's destructor does not throw
	void destroyAt(std::size_t row) noexcept {
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

public:
	std::unordered_map<ComponentId, Archetype*> addEdge;
	std::unordered_map<ComponentId, Archetype*> removeEdge;

	Archetype(std::vector<ComponentId> ids, const std::vector<const ComponentOps*>& ops) {
		componentTypes = std::move(ids);
		
		// ensure that the columns vector has enough space for the component type
		columns.reserve(ops.size());

		for (const ComponentOps* op : ops) {
			columns.emplace_back(op);
		}
	}

	std::size_t size() const noexcept { return entities.size(); }
	const std::vector<ComponentId>& type_ids() const noexcept { return componentTypes; }

	// reserve capacity for at least `n` entities, to avoid vector growth when bulk-creating
	void reserve(std::size_t n) { entities.reserve(n); }

	int columnIndexOf(ComponentId id) const noexcept {
		for (std::size_t i = 0; i < componentTypes.size(); ++i) {
			if (componentTypes[i] == id) {
				return static_cast<int>(i);
			}
		}

		return -1; // not found
	}

	bool contains(ComponentId id) const noexcept { return std::find(componentTypes.begin(), componentTypes.end(), id) != componentTypes.end(); }

	bool containsAll(const std::vector<ComponentId>& ids) const noexcept {
		for (ComponentId id : ids) {
			if (!contains(id)) {
				return false;
			}
		}
		return true;
	}

	Column& column(std::size_t index) noexcept { return columns[index]; }

	std::size_t columnCount() const noexcept { return columns.size(); }

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

	Entity entityAt(std::size_t row) const noexcept { return entities[row]; }
	const std::vector<Entity>& entityList() const noexcept { return entities; }
};

using Signature = std::vector<ComponentId>;

struct SignatureHash {
	std::size_t operator()(const Signature& sig) const noexcept {
		std::size_t h = sig.size();
		for (ComponentId id : sig) {
			// 64-bit variant of boost::hash_combine.
			h ^= id + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
		}
		return h;
	}
};

//--------------------------------------------------------------------------------------------
// 
//--------------------------------------------------------------------------------------------
class ArchetypeRegistry
{
private:
	Archetype *emptyArchetype = nullptr;					// archetype with no components, used for new entities
	std::vector<std::unique_ptr<Archetype>> archetypes;		// a list of existing Archetypes

	// 
	std::unordered_map<Signature, Archetype*, SignatureHash> signatures;

	// find or create an Archetype having the given signature. Set the ops if creating a new one
	Archetype& get_or_create(const Signature& sig, const std::vector<const ComponentOps*>& ops) {
		auto it = signatures.find(sig);
		if (it != signatures.end()) return *it->second;
		return create(sig, ops);
	}

	// create and return a new Archetype having the given signature and ops
	Archetype& create(Signature sig, const std::vector<const ComponentOps*>& ops) {
		auto archetype = std::make_unique<Archetype>(sig, ops);
		Archetype* raw = archetype.get();
		archetypes.push_back(std::move(archetype));
		signatures.emplace(std::move(sig), raw);
		return *raw;
	}

public:
	ArchetypeRegistry() {
		emptyArchetype = &create({}, {});
	}

	Archetype& empty() noexcept { return *emptyArchetype; }

	// Returns the archetype reached from `from` by adding component
	// `added_id` (whose ops are `added_ops`), creating it if necessary.
	Archetype& addTarget(Archetype& from, ComponentId added_id, const ComponentOps* added_ops) {
		auto cached = from.addEdge.find(added_id);
		if (cached != from.addEdge.end()) 
			return *cached->second;

		Signature target_sig = from.type_ids();
		auto insert_at = std::lower_bound(target_sig.begin(), target_sig.end(), added_id);
		target_sig.insert(insert_at, added_id);

		std::vector<const ComponentOps*> ops;
		ops.reserve(target_sig.size());

		for (ComponentId id : target_sig) {
			ops.push_back(id == added_id ? added_ops : from.column(from.columnIndexOf(id)).getOps());
		}

		Archetype& to = get_or_create(target_sig, ops);
		from.addEdge[added_id] = &to;
		to.removeEdge[added_id] = &from;
		return to;
	}

	// Returns the archetype reached from `from` by removing component
	// `removed_id`, creating it if necessary. All ops needed are already
	// known (they come from `from`'s own columns).
	Archetype& removeTarget(Archetype& from, ComponentId removed_id) {
		auto cached = from.removeEdge.find(removed_id);
		if (cached != from.removeEdge.end()) return *cached->second;

		Signature target_sig;
		std::vector<const ComponentOps*> ops;
		target_sig.reserve(from.type_ids().size() - 1);
		ops.reserve(target_sig.capacity());
		for (ComponentId id : from.type_ids()) {
			if (id == removed_id) continue;
			target_sig.push_back(id);
			ops.push_back(from.column(from.columnIndexOf(id)).getOps());
		}

		Archetype& to = get_or_create(target_sig, ops);
		from.removeEdge[removed_id] = &to;
		to.addEdge[removed_id] = &from;
		return to;
	}

	// return all existing archetypes
	const std::vector<std::unique_ptr<Archetype>>& all() const noexcept { return archetypes; }
};
