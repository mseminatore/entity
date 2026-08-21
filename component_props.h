using ComponentId = std::size_t;

//
//
//
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

//
// Archetypes delegate typesafe component operations to this helper
//
struct ComponentOps {
    void (*moveConstruct)(void* dst, void* src);
    void (*destroy)(void* ptr);
    std::size_t size;
    std::size_t alignment;
};

//
//
//
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
