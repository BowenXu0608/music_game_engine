#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <typeindex>
#include <cassert>

using EntityID = uint32_t;
static constexpr EntityID INVALID_ENTITY = 0;

// Dense component storage
template<typename T>
class ComponentPool {
public:
    T& add(EntityID id) {
        assert(m_sparse.find(id) == m_sparse.end());
        m_sparse[id] = static_cast<uint32_t>(m_dense.size());
        m_entities.push_back(id);
        return m_dense.emplace_back();
    }

    void remove(EntityID id) {
        auto it = m_sparse.find(id);
        if (it == m_sparse.end()) return;
        uint32_t idx = it->second;
        uint32_t last = static_cast<uint32_t>(m_dense.size()) - 1;
        if (idx != last) {
            m_dense[idx] = std::move(m_dense[last]);
            m_entities[idx] = m_entities[last];
            m_sparse[m_entities[idx]] = idx;
        }
        m_dense.pop_back();
        m_entities.pop_back();
        m_sparse.erase(it);
    }

    T* get(EntityID id) {
        auto it = m_sparse.find(id);
        return it != m_sparse.end() ? &m_dense[it->second] : nullptr;
    }

    bool has(EntityID id) const { return m_sparse.count(id) > 0; }

    std::vector<T>&        data()     { return m_dense; }
    std::vector<EntityID>& entities() { return m_entities; }

private:
    std::vector<T>        m_dense;
    std::vector<EntityID> m_entities;
    std::unordered_map<EntityID, uint32_t> m_sparse;
};

class Registry {
public:
    EntityID create() { return ++m_nextID; }

    void destroy(EntityID id) {
        for (auto& [type, pool] : m_pools)
            pool->removeRaw(id);
    }

    template<typename T>
    T& emplace(EntityID id) {
        return pool<T>().add(id);
    }

    template<typename T>
    void remove(EntityID id) {
        pool<T>().remove(id);
    }

    template<typename T>
    T* get(EntityID id) {
        return pool<T>().get(id);
    }

    template<typename T>
    bool has(EntityID id) {
        return pool<T>().has(id);
    }

    template<typename T>
    ComponentPool<T>& view() { return pool<T>(); }

private:
    struct IPool {
        virtual void removeRaw(EntityID) = 0;
        virtual ~IPool() = default;
    };
    template<typename T>
    struct TypedPool : IPool, ComponentPool<T> {
        void removeRaw(EntityID id) override { ComponentPool<T>::remove(id); }
    };

    template<typename T>
    ComponentPool<T>& pool() {
        auto key = std::type_index(typeid(T));
        auto it = m_pools.find(key);
        if (it == m_pools.end()) {
            auto p = std::make_unique<TypedPool<T>>();
            auto* raw = p.get();
            m_pools[key] = std::move(p);
            return *raw;
        }
        return *static_cast<TypedPool<T>*>(it->second.get());
    }

    std::unordered_map<std::type_index, std::unique_ptr<IPool>> m_pools;
    EntityID m_nextID = 0;
};
