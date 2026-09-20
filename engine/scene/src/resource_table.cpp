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

ResourceHandle ResourceTable::addSound(std::string_view name, audio::SoundHandle sound) {
    if (sound == audio::kInvalidSound || name.empty()) {
        core::logError("ResourceTable : son ou nom manquant");
        return kInvalidResource;
    }
    const ResourceHandle existing = findSound(name);
    if (existing != kInvalidResource) {
        m_sounds[existing].sound = sound;
        return existing;
    }
    m_sounds.push_back(SoundEntry{std::string(name), sound});
    return static_cast<ResourceHandle>(m_sounds.size() - 1);
}

ResourceHandle ResourceTable::addMaterial(std::string_view name, const Material& material) {
    if (name.empty()) {
        core::logError("ResourceTable : nom de materiau manquant");
        return kInvalidResource;
    }
    const ResourceHandle existing = findMaterial(name);
    if (existing != kInvalidResource) {
        m_materials[existing].material = material;
        return existing;
    }
    m_materials.push_back(MaterialEntry{std::string(name), material});
    return static_cast<ResourceHandle>(m_materials.size() - 1);
}

ResourceHandle ResourceTable::addCollisionMesh(std::string_view name,
                                               const CollisionMesh& mesh) {
    if (name.empty() || !mesh.isValid()) {
        core::logError("ResourceTable : geometrie de collision invalide");
        return kInvalidResource;
    }
    const ResourceHandle existing = findCollisionMesh(name);
    if (existing != kInvalidResource) {
        m_collisionMeshes[existing].mesh = mesh;
        return existing;
    }
    m_collisionMeshes.push_back(CollisionMeshEntry{std::string(name), mesh});
    return static_cast<ResourceHandle>(m_collisionMeshes.size() - 1);
}

ResourceHandle ResourceTable::findMesh(std::string_view name) const {
    return findByName(m_meshes, name);
}

ResourceHandle ResourceTable::findTexture(std::string_view name) const {
    return findByName(m_textures, name);
}

ResourceHandle ResourceTable::findSound(std::string_view name) const {
    return findByName(m_sounds, name);
}

ResourceHandle ResourceTable::findMaterial(std::string_view name) const {
    return findByName(m_materials, name);
}

ResourceHandle ResourceTable::findCollisionMesh(std::string_view name) const {
    return findByName(m_collisionMeshes, name);
}

const rhi::Mesh* ResourceTable::mesh(ResourceHandle handle) const {
    return handle < m_meshes.size() ? m_meshes[handle].mesh : nullptr;
}

const rhi::Texture* ResourceTable::texture(ResourceHandle handle) const {
    return handle < m_textures.size() ? m_textures[handle].texture : nullptr;
}

audio::SoundHandle ResourceTable::sound(ResourceHandle handle) const {
    return handle < m_sounds.size() ? m_sounds[handle].sound : audio::kInvalidSound;
}

const Material* ResourceTable::material(ResourceHandle handle) const {
    return handle < m_materials.size() ? &m_materials[handle].material : nullptr;
}

const CollisionMesh* ResourceTable::collisionMesh(ResourceHandle handle) const {
    return handle < m_collisionMeshes.size() ? &m_collisionMeshes[handle].mesh : nullptr;
}

std::string_view ResourceTable::meshName(ResourceHandle handle) const {
    return handle < m_meshes.size() ? std::string_view(m_meshes[handle].name)
                                    : std::string_view();
}

std::string_view ResourceTable::textureName(ResourceHandle handle) const {
    return handle < m_textures.size() ? std::string_view(m_textures[handle].name)
                                      : std::string_view();
}

std::string_view ResourceTable::soundName(ResourceHandle handle) const {
    return handle < m_sounds.size() ? std::string_view(m_sounds[handle].name)
                                    : std::string_view();
}

std::string_view ResourceTable::materialName(ResourceHandle handle) const {
    return handle < m_materials.size() ? std::string_view(m_materials[handle].name)
                                       : std::string_view();
}

std::string_view ResourceTable::collisionMeshName(ResourceHandle handle) const {
    return handle < m_collisionMeshes.size()
               ? std::string_view(m_collisionMeshes[handle].name)
               : std::string_view();
}

} // namespace scene
