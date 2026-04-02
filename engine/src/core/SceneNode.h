#pragma once
#include "Transform.h"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdint>

using NodeID = uint32_t;
static constexpr NodeID INVALID_NODE = UINT32_MAX;

class SceneNode {
public:
    Transform localTransform;

    NodeID id()     const { return m_id; }
    NodeID parent() const { return m_parent; }

    const std::vector<NodeID>& children() const { return m_children; }

    glm::mat4 worldMatrix() const {
        if (m_dirty) {
            m_worldMatrix = localTransform.toMatrix();
            // parent resolution done by SceneGraph
            m_dirty = false;
        }
        return m_worldMatrix;
    }

    void markDirty() {
        m_dirty = true;
        // children are marked dirty by SceneGraph::markDirtyRecursive
    }

    bool isDirty() const { return m_dirty; }

private:
    friend class SceneGraph;
    NodeID m_id     = INVALID_NODE;
    NodeID m_parent = INVALID_NODE;
    std::vector<NodeID> m_children;
    mutable glm::mat4 m_worldMatrix{1.f};
    mutable bool m_dirty = true;
};

// ── SceneGraph ───────────────────────────────────────────────────────────────

class SceneGraph {
public:
    NodeID createNode(NodeID parent = INVALID_NODE) {
        NodeID id = m_nextID++;
        SceneNode& node = m_nodes[id];
        node.m_id     = id;
        node.m_parent = parent;
        node.m_dirty  = true;
        if (parent != INVALID_NODE)
            m_nodes[parent].m_children.push_back(id);
        else
            m_roots.push_back(id);
        return id;
    }

    void destroyNode(NodeID id) {
        auto it = m_nodes.find(id);
        if (it == m_nodes.end()) return;
        SceneNode& node = it->second;
        if (node.m_parent != INVALID_NODE) {
            auto& siblings = m_nodes[node.m_parent].m_children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
        } else {
            m_roots.erase(std::remove(m_roots.begin(), m_roots.end(), id), m_roots.end());
        }
        for (NodeID child : node.m_children) destroyNode(child);
        m_nodes.erase(it);
    }

    SceneNode* get(NodeID id) {
        auto it = m_nodes.find(id);
        return it != m_nodes.end() ? &it->second : nullptr;
    }

    void markDirty(NodeID id) { markDirtyRecursive(id); }

    void update() {
        for (NodeID root : m_roots)
            updateNode(root, glm::mat4(1.f), false);
    }

    glm::mat4 worldMatrix(NodeID id) const {
        auto it = m_nodes.find(id);
        return it != m_nodes.end() ? it->second.m_worldMatrix : glm::mat4(1.f);
    }

private:
    void markDirtyRecursive(NodeID id) {
        auto it = m_nodes.find(id);
        if (it == m_nodes.end()) return;
        it->second.m_dirty = true;
        for (NodeID child : it->second.m_children) markDirtyRecursive(child);
    }

    void updateNode(NodeID id, const glm::mat4& parentWorld, bool parentDirty) {
        auto it = m_nodes.find(id);
        if (it == m_nodes.end()) return;
        SceneNode& node = it->second;
        bool needsUpdate = node.m_dirty || parentDirty;
        if (needsUpdate) {
            node.m_worldMatrix = parentWorld * node.localTransform.toMatrix();
            node.m_dirty = false;
        }
        for (NodeID child : node.m_children)
            updateNode(child, node.m_worldMatrix, needsUpdate);
    }

    std::unordered_map<NodeID, SceneNode> m_nodes;
    std::vector<NodeID> m_roots;
    NodeID m_nextID = 0;
};
