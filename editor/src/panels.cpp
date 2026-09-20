#include "panels.h"

#include "scene/audio_sync.h"
#include "scene/components.h"
#include "scene/footsteps.h"
#include "scene/physics_sync.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <imgui.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <glm/gtc/quaternion.hpp>

#include <string>

namespace editor {
namespace {

constexpr float kDragSpeedPosition = 0.01f;
constexpr float kDragSpeedAngle = 0.5f;
constexpr float kDragSpeedScale = 0.01f;

// Choix d'une ressource parmi celles que la table connait.
//
// Les poignees sont des indices consecutifs : enumerer revient donc a compter. C'est la
// raison pour laquelle la table expose des compteurs plutot qu'un iterateur - il n'y a
// rien a iterer, juste un intervalle a parcourir.
//
// Rend vrai si le choix a change.
template <typename NameOf>
bool resourceCombo(const char* label, scene::ResourceHandle& handle, core::u32 count,
                   NameOf nameOf) {
    const std::string current =
        handle < count ? std::string(nameOf(handle)) : std::string("(aucune)");
    bool changed = false;
    if (ImGui::BeginCombo(label, current.c_str())) {
        // "(aucune)" est une valeur legitime : une carte de normales est facultative, et
        // une source audio sans son est un objet muet qu'on a le droit de vouloir.
        if (ImGui::Selectable("(aucune)", handle >= count)) {
            handle = scene::kInvalidResource;
            changed = true;
        }
        for (core::u32 i = 0; i < count; ++i) {
            const std::string name(nameOf(i));
            // L'identifiant ImGui vient de l'INDICE : deux ressources homonymes seraient
            // sinon confondues dans la liste.
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(name.c_str(), handle == i)) {
                handle = i;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// En-tete d'un composant, avec son bouton de retrait. Rend vrai si le contenu doit etre
// dessine, et met `removed` a vrai si l'utilisateur a demande la suppression.
bool componentHeader(const char* label, bool& removed) {
    const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PushID(label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
    if (ImGui::SmallButton("x")) {
        removed = true;
    }
    ImGui::PopID();
    return open;
}

void drawTransform(scene::Transform& transform) {
    ImGui::DragFloat3("position", &transform.position.x, kDragSpeedPosition);

    // Des DEGRES a l'ecran, un quaternion en memoire. La conversion n'a lieu que si l'on
    // touche au champ : la refaire a chaque image ferait deriver les dernieres decimales,
    // et un objet immobile finirait par tourner tout seul.
    core::Vec3 degrees = glm::degrees(glm::eulerAngles(transform.rotation));
    if (ImGui::DragFloat3("rotation", &degrees.x, kDragSpeedAngle)) {
        transform.rotation = core::Quat(glm::radians(degrees));
    }
    ImGui::DragFloat3("echelle", &transform.scale.x, kDragSpeedScale);
}

void drawMeshRenderer(scene::MeshRenderer& mesh, const scene::ResourceTable& resources) {
    resourceCombo("maillage", mesh.mesh, resources.meshCount(),
                  [&](core::u32 i) { return resources.meshName(i); });
    resourceCombo("matiere", mesh.material, resources.materialCount(),
                  [&](core::u32 i) { return resources.materialName(i); });
}

void drawLight(scene::LightSource& light) {
    ImGui::ColorEdit3("couleur", &light.color.x);
    // L'intensite est PHYSIQUE : elle se divise par le carre de la distance. D'ou des
    // valeurs qui paraissent enormes - 160 pour une lampe torche - et une plage large.
    ImGui::DragFloat("intensite", &light.intensity, 0.5f, 0.0f, 1000.0f);
    ImGui::DragFloat("portee", &light.range, 0.1f, 0.1f, 200.0f);

    int type = light.type == renderer::LightType::Spot ? 1 : 0;
    if (ImGui::Combo("type", &type, "ponctuelle\0spot\0")) {
        light.type = type == 1 ? renderer::LightType::Spot : renderer::LightType::Point;
    }
    if (light.type == renderer::LightType::Spot) {
        core::f32 inner = glm::degrees(light.innerAngleRadians);
        core::f32 outer = glm::degrees(light.outerAngleRadians);
        bool changed = ImGui::DragFloat("cone interieur", &inner, 0.5f, 0.0f, 89.0f);
        changed |= ImGui::DragFloat("cone exterieur", &outer, 0.5f, 0.0f, 89.0f);
        if (changed) {
            // Le cone interieur ne peut pas depasser l'exterieur : le degrade entre les
            // deux s'inverserait, et le bord du faisceau deviendrait une decoupe nette.
            inner = std::min(inner, outer);
            light.innerAngleRadians = glm::radians(inner);
            light.outerAngleRadians = glm::radians(outer);
        }
    }
    ImGui::Checkbox("projette une ombre", &light.castsShadow);
}

void drawCollider(scene::Collider& collider, const scene::ResourceTable& resources) {
    int shape = collider.shape == scene::ColliderShape::Mesh ? 1 : 0;
    if (ImGui::Combo("forme", &shape, "boite\0maillage\0")) {
        collider.shape =
            shape == 1 ? scene::ColliderShape::Mesh : scene::ColliderShape::Box;
    }

    if (collider.shape == scene::ColliderShape::Mesh) {
        resourceCombo("geometrie", collider.collisionMesh, resources.collisionMeshCount(),
                      [&](core::u32 i) { return resources.collisionMeshName(i); });
        // Un maillage de triangles n'a ni volume ni masse bien definis : Jolt refuse de
        // le faire bouger, et l'interface ne doit pas laisser croire le contraire.
        collider.isStatic = true;
        ImGui::TextDisabled("un maillage est toujours statique");
        return;
    }

    ImGui::DragFloat3("demi-dimensions", &collider.halfExtents.x, kDragSpeedScale, 0.01f,
                      100.0f);
    ImGui::Checkbox("statique", &collider.isStatic);
    if (!collider.isStatic) {
        // 1000 kg/m3 est la valeur par defaut de Jolt, celle de l'eau. Une porte en bois
        // plein est a 300, un battant creux a 150 : le rappel evite de refaire l'erreur
        // du battant de 144 kg.
        ImGui::DragFloat("masse volumique", &collider.density, 5.0f, 10.0f, 5000.0f,
                         "%.0f kg/m3");
    }
}

void drawAudioSource(scene::AudioSource& source, const scene::ResourceTable& resources) {
    resourceCombo("son", source.sound, resources.soundCount(),
                  [&](core::u32 i) { return resources.soundName(i); });
    ImGui::SliderFloat("volume", &source.volume, 0.0f, 1.0f);
    ImGui::Checkbox("en boucle", &source.looping);
    ImGui::TextDisabled("un changement prend effet au rechargement");
}

void drawSurface(scene::Surface& surface, const scene::ResourceTable& resources) {
    resourceCombo("son de pas", surface.footstep, resources.soundCount(),
                  [&](core::u32 i) { return resources.soundName(i); });
}

// Ajout d'un composant. Seuls ceux qui manquent sont proposes : offrir d'ajouter ce qui
// est deja la ne ferait qu'allonger la liste.
void drawAddComponent(scene::Scene& scene, scene::Entity entity) {
    entt::registry& registry = scene.registry();
    if (!ImGui::BeginCombo("##ajouter", "Ajouter un composant")) {
        return;
    }
    if (registry.try_get<scene::MeshRenderer>(entity) == nullptr &&
        ImGui::Selectable("maillage")) {
        registry.emplace<scene::MeshRenderer>(entity);
    }
    if (registry.try_get<scene::LightSource>(entity) == nullptr &&
        ImGui::Selectable("lumiere")) {
        registry.emplace<scene::LightSource>(entity);
    }
    if (registry.try_get<scene::Collider>(entity) == nullptr &&
        ImGui::Selectable("collider")) {
        registry.emplace<scene::Collider>(entity);
    }
    if (registry.try_get<scene::AudioSource>(entity) == nullptr &&
        ImGui::Selectable("source audio")) {
        registry.emplace<scene::AudioSource>(entity);
    }
    if (registry.try_get<scene::Surface>(entity) == nullptr &&
        ImGui::Selectable("matiere de surface")) {
        registry.emplace<scene::Surface>(entity);
    }
    ImGui::EndCombo();
}

} // namespace

void drawComponentPanels(scene::Scene& scene, const scene::ResourceTable& resources,
                         scene::Entity entity) {
    entt::registry& registry = scene.registry();

    if (auto* transform = registry.try_get<scene::Transform>(entity); transform != nullptr) {
        // Le Transform n'a pas de bouton de retrait : sans position, un objet n'est nulle
        // part. C'est le seul composant que la scene impose.
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawTransform(*transform);
        }
    }

    // Le retrait est demande pendant le dessin et applique APRES : retirer un composant
    // dont on est en train d'afficher les champs libererait la memoire qu'ils lisent.
    bool removeMesh = false;
    if (auto* mesh = registry.try_get<scene::MeshRenderer>(entity); mesh != nullptr) {
        if (componentHeader("Maillage", removeMesh)) {
            drawMeshRenderer(*mesh, resources);
        }
    }

    bool removeLight = false;
    if (auto* light = registry.try_get<scene::LightSource>(entity); light != nullptr) {
        if (componentHeader("Lumiere", removeLight)) {
            drawLight(*light);
        }
    }

    bool removeCollider = false;
    if (auto* collider = registry.try_get<scene::Collider>(entity); collider != nullptr) {
        if (componentHeader("Collider", removeCollider)) {
            drawCollider(*collider, resources);
            ImGui::TextDisabled("le corps physique est cree au chargement");
        }
    }

    bool removeAudio = false;
    if (auto* audio = registry.try_get<scene::AudioSource>(entity); audio != nullptr) {
        if (componentHeader("Source audio", removeAudio)) {
            drawAudioSource(*audio, resources);
        }
    }

    bool removeSurface = false;
    if (auto* surface = registry.try_get<scene::Surface>(entity); surface != nullptr) {
        if (componentHeader("Matiere de surface", removeSurface)) {
            drawSurface(*surface, resources);
        }
    }

    if (removeMesh) {
        registry.remove<scene::MeshRenderer>(entity);
    }
    if (removeLight) {
        registry.remove<scene::LightSource>(entity);
    }
    if (removeCollider) {
        registry.remove<scene::Collider>(entity);
    }
    if (removeAudio) {
        registry.remove<scene::AudioSource>(entity);
    }
    if (removeSurface) {
        registry.remove<scene::Surface>(entity);
    }

    ImGui::Separator();
    drawAddComponent(scene, entity);
}

} // namespace editor
