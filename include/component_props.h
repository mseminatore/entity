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

//--------------------------------------------------------------------------------------------
// Define the ComponentType and ComponentOps classes, which are used to manage component types
// in an ECS (Entity-Component-System) architecture.
//--------------------------------------------------------------------------------------------

// Define the type for component IDs
using ComponentId = std::size_t;

//--------------------------------------------------------------------------------------------
// The ComponentType class generates unique identifiers for each component type T
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
// The ComponentSetType class generates a unique, process-wide id for each distinct ordered
// pack of component types. Unlike ComponentType::get<T>() (one id per component), this gives
// callers a way to index a small per-instance cache (e.g. a vector) by "which
// create<Components...>() instantiation is this", with no hashing or allocation involved.
//--------------------------------------------------------------------------------------------
class ComponentSetType
{
private:
    static ComponentId next() noexcept {
        static ComponentId counter = 0;
        return counter++;
    }

public:
    template <typename... Components>
    static ComponentId get() noexcept {
        static const ComponentId id = next();
        return id;
    }
};

//--------------------------------------------------------------------------------------------
// Archetypes delegate typesafe component operations to this helper.
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
