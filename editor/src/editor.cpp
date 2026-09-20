#include "editor/editor.h"

#include "core/log.h"
#include "platform/input.h"
#include "platform/window.h"
#include "scene/components.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace editor {
namespace {

// Version de GLSL declaree aux shaders internes d'ImGui. Elle doit correspondre au
// contexte cree par la plateforme - OpenGL 4.6 coeur.
constexpr const char* kGlslVersion = "#version 460 core";

// L'editeur ne doit pas obliger a garder Maj enfonce pour taper un nombre : les champs
// numeriques d'ImGui avancent d'autant plus vite qu'on glisse loin.
constexpr float kDragSpeedPosition = 0.01f; // metres par pixel
constexpr float kDragSpeedAngle = 0.5f;     // degres par pixel
constexpr float kDragSpeedScale = 0.01f;

void forwardEvent(const void* sdlEvent, void* /*user*/) {
    ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdlEvent));
}

// Nom affichable d'une entite : son Name si elle en a un, sinon son numero. Une entite
// sans nom existe - rien ne l'impose - et elle doit rester selectionnable.
std::string displayName(const scene::Scene& scene, scene::Entity entity) {
    const auto* name = scene.registry().try_get<scene::Name>(entity);
    if (name != nullptr && !name->value.empty()) {
        return name->value;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "entite %u",
                  static_cast<core::u32>(entt::to_integral(entity)));
    return buffer;
}

} // namespace

struct Editor::Impl {
    platform::Window* window = nullptr;
    bool visible = false;
    scene::Entity selected = scene::kInvalidEntity;
    // Reutilise d'une image a l'autre : construire la liste des enfants ne doit pas
    // allouer soixante fois par seconde.
    std::vector<scene::Entity> children;
};

Editor::Editor() = default;

Editor::~Editor() { destroy(); }

bool Editor::create(platform::Window& window, platform::Input& input) {
    if (m_impl != nullptr) {
        return true;
    }
    auto impl = std::make_unique<Impl>();
    impl->window = &window;

    IMGUI_CHECKVERSION();
    if (ImGui::CreateContext() == nullptr) {
        core::logError("editeur : creation du contexte ImGui impossible");
        return false;
    }
    ImGui::StyleColorsDark();

    auto* sdlWindow = static_cast<SDL_Window*>(window.nativeWindow());
    auto* glContext = static_cast<SDL_GLContext>(window.nativeGlContext());
    if (sdlWindow == nullptr || glContext == nullptr) {
        core::logError("editeur : fenetre ou contexte GPU indisponible");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplSDL3_InitForOpenGL(sdlWindow, glContext) ||
        !ImGui_ImplOpenGL3_Init(kGlslVersion)) {
        core::logError("editeur : initialisation des liaisons ImGui echouee");
        ImGui::DestroyContext();
        return false;
    }

    // L'editeur voit les evenements bruts. Il les voit MEME masque : ImGui doit garder un
    // etat de clavier coherent, sinon une touche relachee pendant qu'il etait cache
    // resterait enfoncee a sa reapparition.
    input.setRawEventObserver(&forwardEvent, impl.get());

    m_impl = std::move(impl);
    core::logInfo("editeur pret");
    return true;
}

void Editor::destroy() {
    if (m_impl == nullptr) {
        return;
    }
    // Ordre inverse de la creation : les liaisons avant le contexte, sans quoi elles
    // libereraient des ressources appartenant a un contexte deja detruit.
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    m_impl.reset();
}

void Editor::setVisible(bool visible) {
    if (m_impl != nullptr) {
        m_impl->visible = visible;
    }
}

void Editor::toggle() {
    if (m_impl != nullptr) {
        m_impl->visible = !m_impl->visible;
    }
}

bool Editor::isVisible() const { return m_impl != nullptr && m_impl->visible; }

bool Editor::capturesMouse() const {
    return isVisible() && ImGui::GetIO().WantCaptureMouse;
}

bool Editor::capturesKeyboard() const {
    return isVisible() && ImGui::GetIO().WantCaptureKeyboard;
}

scene::Entity Editor::selected() const {
    return m_impl != nullptr ? m_impl->selected : scene::kInvalidEntity;
}

namespace {

// Dessine une entite et, recursivement, celles qui la declarent pour parent.
void drawHierarchyNode(scene::Scene& scene, scene::Entity entity, scene::Entity& selected,
                       std::vector<scene::Entity>& scratch) {
    // Les enfants sont retrouves par balayage : la scene stocke le lien dans l'ENFANT, pas
    // dans le parent. Une table inverse serait plus rapide ; elle attendra qu'un niveau
    // compte assez d'entites pour que ce parcours se voie.
    scratch.clear();
    for (auto [candidate, parent] : scene.registry().view<const scene::Parent>().each()) {
        if (parent.value == entity) {
            scratch.push_back(candidate);
        }
    }
    // Copie locale : la recursion va reutiliser le tampon partage.
    const std::vector<scene::Entity> childrenHere = scratch;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (childrenHere.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (entity == selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const std::string label = displayName(scene, entity);
    // L'identifiant ImGui vient de l'ENTITE et non du nom : deux entites homonymes sont
    // legitimes, et partager un identifiant les ferait s'ouvrir et se selectionner
    // ensemble.
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::uintptr_t>(
                                            entt::to_integral(entity))),
                                        flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selected = entity;
    }
    if (open) {
        for (const scene::Entity child : childrenHere) {
            drawHierarchyNode(scene, child, selected, scratch);
        }
        ImGui::TreePop();
    }
}

void drawHierarchy(scene::Scene& scene, scene::Entity& selected,
                   std::vector<scene::Entity>& scratch);

// Liste, en lecture seule, ce que porte l'entite. Savoir de quoi un objet est fait est la
// premiere question qu'on se pose devant un editeur.
void drawComponentSummary(const scene::Scene& scene, scene::Entity entity) {
    const entt::registry& registry = scene.registry();
    struct Row {
        const char* label;
        bool present;
    };
    const Row rows[] = {
        {"maillage", registry.try_get<scene::MeshRenderer>(entity) != nullptr},
        {"lumiere", registry.try_get<scene::LightSource>(entity) != nullptr},
        {"parent", registry.try_get<scene::Parent>(entity) != nullptr},
    };
    for (const Row& row : rows) {
        if (row.present) {
            ImGui::BulletText("%s", row.label);
        }
    }
}

} // namespace

void Editor::draw(scene::Scene& scene) {
    if (m_impl == nullptr) {
        return;
    }
    Impl& impl = *m_impl;

    // La selection peut pointer sur une entite detruite entre deux images : on verifie
    // avant de s'en servir plutot que d'avoir a y penser dans chaque panneau.
    if (impl.selected != scene::kInvalidEntity && !scene.isValid(impl.selected)) {
        impl.selected = scene::kInvalidEntity;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (impl.visible) {
        drawHierarchy(scene, impl.selected, impl.children);
    }

    ImGui::Render();
    // Le rendu se fait meme masque : ImGui exige qu'une image commencee soit terminee, et
    // une liste de commandes vide ne coute rien.
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

namespace {

void drawHierarchy(scene::Scene& scene, scene::Entity& selected,
                   std::vector<scene::Entity>& scratch) {
    ImGui::SetNextWindowSize(ImVec2(280.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Hierarchie")) {
        core::u32 count = 0;
        for (auto entity : scene.registry().view<entt::entity>()) {
            (void)entity;
            ++count;
        }
        ImGui::TextDisabled("%u entites", count);
        ImGui::Separator();

        // Seules les RACINES sont parcourues ici : les enfants sont dessines par leur
        // parent, ce qui donne l'arbre sans avoir a le construire.
        for (auto entity : scene.registry().view<entt::entity>()) {
            const auto* parent = scene.registry().try_get<scene::Parent>(entity);
            const bool isRoot =
                parent == nullptr || !scene.registry().valid(parent->value);
            if (isRoot) {
                drawHierarchyNode(scene, entity, selected, scratch);
            }
        }
    }
    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(320.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(312.0f, 16.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Inspecteur")) {
        if (selected == scene::kInvalidEntity) {
            ImGui::TextDisabled("Aucune entite selectionnee.");
        } else {
            ImGui::TextUnformatted(displayName(scene, selected).c_str());
            ImGui::Separator();

            auto* transform = scene.registry().try_get<scene::Transform>(selected);
            if (transform != nullptr && ImGui::CollapsingHeader("Transform",
                                                                ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("position", &transform->position.x, kDragSpeedPosition);

                // Des DEGRES a l'ecran, un quaternion en memoire. C'est ce que le SPEC
                // annoncait des M4 : personne ne raisonne en quaternions, et personne ne
                // veut d'un format de fichier qui souffre du blocage de cardan.
                //
                // La conversion n'a lieu que si l'utilisateur touche au champ : la
                // refaire a chaque image ferait deriver les dernieres decimales, et un
                // objet immobile finirait par tourner tout seul.
                core::Vec3 degrees = glm::degrees(glm::eulerAngles(transform->rotation));
                if (ImGui::DragFloat3("rotation", &degrees.x, kDragSpeedAngle)) {
                    transform->rotation = core::Quat(glm::radians(degrees));
                }

                ImGui::DragFloat3("echelle", &transform->scale.x, kDragSpeedScale);
            }

            if (ImGui::CollapsingHeader("Composants", ImGuiTreeNodeFlags_DefaultOpen)) {
                drawComponentSummary(scene, selected);
            }
        }
    }
    ImGui::End();
}

} // namespace

} // namespace editor
