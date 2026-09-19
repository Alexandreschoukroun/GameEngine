#include "scene/resource_table.h"

#include "core/log.h"

namespace scene {
namespace {

template <typename Container>
ResourceHandle findByName(const Container& entries, std::string_view name) {
    for (core::u32 i = 0; i < entries.size(); ++i) {
        if (entries[i].name == name) {
            return i;
        }
    }
    return kInvalidResource;
}

} // namespace

ResourceHandle ResourceTable::addMesh(std::string_view name, const rhi::Mesh* mesh) {
    if (mesh == nullptr || name.empty()) {
        core::logError("ResourceTable : maillage ou nom manquant");
        return kInvalidResource;
    }
    const ResourceHandle existing = findMesh(name);
    if (existing != kInvalidResource) {
        // Reenregistrer sous le meme nom remplace la ressource : les scenes deja chargees
        // continuent de pointer sur la bonne poignee.
        m_meshes[existing].mesh = mesh;
        return existing;
    }
    m_meshes.push_back(MeshEntry{std::string(name), mesh});
    return static_cast<ResourceHandle>(m_meshes.size() - 1);
}

ResourceHandle ResourceTable::addTexture(std::string_view name, const rhi::Texture* texture) {
    if (texture == nullptr || name.empty()) {
        core::logError("ResourceTable : texture ou nom manquant");
        return kInvalidResource;
    }
    const ResourceHandle existing = findTexture(name);
    if (existing != kInvalidResource) {
        m_textures[existing].texture = texture;
        return existing;
    }
    m_textures.push_back(TextureEntry{std::string(name), texture});
    return static_cast<ResourceHandle>(m_textures.size() - 1);
}

ResourceHandle ResourceTable::findMesh(std::string_view name) const {
    return findByName(m_meshes, name);
}

ResourceHandle ResourceTable::findTexture(std::string_view name) const {
    return findByName(m_textures, name);
}

const rhi::Mesh* ResourceTable::mesh(ResourceHandle handle) const {
    return handle < m_meshes.size() ? m_meshes[handle].mesh : nullptr;
}

const rhi::Texture* ResourceTable::texture(ResourceHandle handle) const {
    return handle < m_textures.size() ? m_textures[handle].texture : nullptr;
}

std::string_view ResourceTable::meshName(ResourceHandle handle) const {
    return handle < m_meshes.size() ? std::string_view(m_meshes[handle].name)
                                    : std::string_view();
}

std::string_view ResourceTable::textureName(ResourceHandle handle) const {
    return handle < m_textures.size() ? std::string_view(m_textures[handle].name)
                                      : std::string_view();
}

} // namespace scene
