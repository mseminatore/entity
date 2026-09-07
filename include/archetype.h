//------------------------------------------------------
// Copyright (C) 2026 Mark Seminatore
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is furnished
// to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//------------------------------------------------------

#pragma once

#include <cstddef>
#include <new>
#include <unordered_map>
#include <memory>

//-----------------------------------------------------------------------------
// Storage for a single component type, which is a vector of component data for
// each entity in the archetype
//-----------------------------------------------------------------------------
class Column
{
private:
	constexpr static std::size_t MinCapacity = 4;	// minimum capacity for a column, to avoid frequent reallocations
	constexpr static std::size_t GrowthFactor = 2;	// factor by which to grow the column capacity when needed

	std::byte* data = nullptr;						// growable storage for component data
	std::size_t count = 0;							// current number of components
	std::size_t capacity = 0;						// size of component array
	const ComponentOps* ops = nullptr;

	// Grow storage by GrowthFactor
	void grow() {
		std::size_t new_capacity = capacity == 0 ? MinCapacity : capacity * GrowthFactor;
		auto* new_data = static_cast<std::byte*>(::operator new(new_capacity * ops->size, std::align_val_t{ ops->alignment }));

		for (std::size_t i = 0; i < count; i++) {
			ops->moveConstruct(new_data + i * ops->size, data + i * ops->size);
		}

		releaseBuffer();

		data		= new_data;
		capacity	= new_capacity;
	}

	// Release the memory block
	void releaseBuffer() noexcept {
		if (data)
			::operator delete(data, std::align_val_t{ ops->alignment });
	}

	// Destroy each object before releasing the memory block. Assumes component
	// destructors do not throw, matching the standard convention for destructors.
	void release() noexcept {
		if (data) {
			for (std::size_t i = 0; i < count; i++) {
				ops->destroy(at(i));
			}

			releaseBuffer();

			data		= nullptr;
			count		= 0;
			capacity	= 0;
		}
	}

public:
	// Constructor
	Column(const ComponentOps* ops) noexcept : ops(ops) {}
	
	// Copy constructor
	Column(const Column&) = delete;

	// Disallow copy assignment
	Column& operator=(const Column&) = delete;

	// Move constructor
	Column(Column&& other) noexcept
		: data(other.data), count(other.count), capacity(other.capacity), ops(other.ops) {
		other.data = nullptr;
		other.count = 0;
		other.capacity = 0;
	}

	// Move assignment
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

	// Destructor for the Column class, releases all allocated resources
	~Column() { release(); }

	// Get the number of components currently stored in the column
	std::size_t size() const noexcept { return count; }

	// Get the ComponentOps associated with this column
	const ComponentOps* getOps() const noexcept { return ops; }

	// Get a pointer to the component at the specified row
	void* at(std::size_t row) noexcept { return data + row * ops->size; }

	// Add a new uninitialized component to the column and return a pointer to it
	void* pushUninitialized() {
		// grow if we are full
		if (count == capacity)
			grow();

		return data + (count++) * ops->size;
	}

	// Assumes the component's destructor does not throw
	void destroyAt(std::size_t row) noexcept {
		ops->destroy(at(row));
	}

	// Move the last row into the specified row and decrease the row count
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

//-----------------------------------------------------------------------------
// Storage for a collection of columns, representing a unique combination of 
// component types
//-----------------------------------------------------------------------------
class Archetype
{
private:
	std::vector<ComponentId> componentTypes;	// unique combination of component types for this archetype
	std::vector<Column> columns;				// storage for each component type in the archetype
	std::vector<Entity> entities;				// list of entities in this archetype

public:
	// Bad form, but necessary for quickly navigating between archetypes when
	// adding or removing components
	std::unordered_map<ComponentId, Archetype*> addEdge;
	std::unordered_map<ComponentId, Archetype*> removeEdge;

	// Constructor for the Archetype class, initializes the component types and 
	// columns based on the provided component operations
	Archetype(std::vector<ComponentId> ids, const std::vector<const ComponentOps*>& ops) {
		componentTypes = std::move(ids);
		
		// ensure that the columns vector has enough space for the component type
		columns.reserve(ops.size());

		for (const ComponentOps* op : ops) {
			columns.emplace_back(op);
		}
	}

	// Get the number of entities currently stored in the archetype
	std::size_t size() const noexcept { return entities.size(); }

	// Get the list of component type IDs for this archetype
	const std::vector<ComponentId>& type_ids() const noexcept { return componentTypes; }

	// Reserve capacity for at least `n` entities, to avoid vector growth when bulk-creating
	void reserve(std::size_t n) { entities.reserve(n); }

	// Get the index of the column corresponding to the specified component ID, or -1 if not found
	int columnIndexOf(ComponentId id) const noexcept {
		for (std::size_t i = 0; i < componentTypes.size(); ++i) {
			if (componentTypes[i] == id) {
				return static_cast<int>(i);
			}
		}

		return -1; // not found, maybe switch to std::optional?
	}

	// Check if the archetype contains the specified component ID
	bool contains(ComponentId id) const noexcept { return std::find(componentTypes.begin(), componentTypes.end(), id) != componentTypes.end(); }

	// Check if the archetype contains all of the specified component IDs
	bool containsAll(const std::vector<ComponentId>& ids) const noexcept {
		for (ComponentId id : ids) {
			if (!contains(id)) {
				return false;
			}
		}
		return true;
	}

	// Get the column at the specified index
	Column& column(std::size_t index) noexcept { return columns[index]; }

	// Get the number of columns in the archetype
	std::size_t columnCount() const noexcept { return columns.size(); }

	// Add a new entity with uninitialized components to the archetype and return its row index
	std::size_t pushUninitializedRow(Entity entity) {

		// add a new entity with empty components to the archetype and return its row index
		for (auto &col : columns) {
			// add a new row to each column for the new entity
			col.pushUninitialized();
		}

		entities.push_back(entity);
		return entities.size() - 1;
	}

	// Destroy the components of the entity at the specified row and remove it from the archetype
	Entity removeRow(std::size_t row) {
		for (auto &col : columns) {
			col.destroyAt(row);
		}

		return finishRemovingRow(row);
	}

	// Move the last entity into the specified row and return the moved entity, or NullEntity if the removed row was the last row
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

	// Get the entity at the specified row
	Entity entityAt(std::size_t row) const noexcept { return entities[row]; }

	// Get the list of all entities in the archetype
	const std::vector<Entity>& entityList() const noexcept { return entities; }
};

// An ascending list of component IDs representing an archetype's signature
using Signature = std::vector<ComponentId>;

// Hash function for signatures, used in the unordered_map of archetypes by signature
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
// The registry tracks all of the archetypes in the system
//--------------------------------------------------------------------------------------------
class ArchetypeRegistry
{
private:
	Archetype *emptyArchetype = nullptr;					// archetype with no components, used for new entities
	std::vector<std::unique_ptr<Archetype>> archetypes;		// a list of existing Archetypes

	// Map from archetype signatures to their corresponding Archetype pointers
	std::unordered_map<Signature, Archetype*, SignatureHash> signatures;

	// Find or create an Archetype having the given signature. Set the ops if creating a new one
	Archetype& get_or_create(const Signature& sig, const std::vector<const ComponentOps*>& ops) {
		auto it = signatures.find(sig);
		if (it != signatures.end()) return *it->second;
		return create(sig, ops);
	}

	// Create and return a new Archetype having the given signature and ops
	Archetype& create(Signature sig, const std::vector<const ComponentOps*>& ops) {
		auto archetype = std::make_unique<Archetype>(sig, ops);
		Archetype* raw = archetype.get();
		archetypes.push_back(std::move(archetype));
		signatures.emplace(std::move(sig), raw);
		return *raw;
	}

public:
	// Constructor initializes an empty archetype
	ArchetypeRegistry() {
		emptyArchetype = &create({}, {});
	}

	// Get the empty archetype (with no components)
	Archetype& empty() noexcept { return *emptyArchetype; }

	// Returns the archetype for the given signature, creating it if necessary. Unlike
	// addTarget/removeTarget, this doesn't require starting from an existing archetype --
	// used by EntityManager::create<Components...>() to place a new entity directly into its
	// final archetype in one step. `sig` must already be canonical (strictly ascending,
	// duplicate-free) -- the same order addTarget/removeTarget chains always produce -- since
	// a non-canonical signature would intern as a distinct (bogus) archetype rather than
	// matching the one addTarget/removeTarget would reach for the same component set.
	Archetype& forSignature(const Signature& sig, const std::vector<const ComponentOps*>& ops) {
		for (std::size_t i = 1; i < sig.size(); ++i)
			ENTITY_ASSERT(sig[i - 1] < sig[i] && "forSignature() requires a canonical (ascending, duplicate-free) signature");

		return get_or_create(sig, ops);
	}

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

	// Return all existing archetypes
	const std::vector<std::unique_ptr<Archetype>>& all() const noexcept { return archetypes; }
};
