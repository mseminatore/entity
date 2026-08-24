#pragma once

//--------------------------------------------------------------------------------------------
// Define the ComponentType and ComponentOps classes, which are used to manage component types
// in an ECS (Entity-Component-System) architecture.
//--------------------------------------------------------------------------------------------

// define the type for component IDs
using ComponentId = std::size_t;

//--------------------------------------------------------------------------------------------
// The ComponentType class generates unique identifiers for each component type
//--------------------------------------------------------------------------------------------
class ComponentType
{
private:
    static ComponentId next() noexcept {
        static ComponentId counter = 0;
        return counter++;
    }

public:
    template <typename T>
    static ComponentId get() noexcept {
        static const ComponentId id = next();
        return id;
    }
};

//--------------------------------------------------------------------------------------------
// Archetypes delegate typesafe component operations to this helper
// The ComponentOps struct defines operations for moving and destroying components
// as well as their size and alignment. 
//--------------------------------------------------------------------------------------------
struct ComponentOps {
    void (*moveConstruct)(void* dst, void* src);
    void (*destroy)(void* ptr);
    std::size_t size;
    std::size_t alignment;
};

//--------------------------------------------------------------------------------------------
// The get_component_ops function template returns a pointer to a ComponentOps instance for 
// a given component type T.
//--------------------------------------------------------------------------------------------
template <typename T>
const ComponentOps* get_component_ops() noexcept {
    static const ComponentOps ops{
        [](void* dst, void* src) {
            new (dst) T(std::move(*static_cast<T*>(src)));
            static_cast<T*>(src)->~T();
        },
        [](void* ptr) { static_cast<T*>(ptr)->~T(); },
        sizeof(T),
        alignof(T),
    };
    return &ops;
}
