#include "editor/editor.h"

#include "panels.h"

#include "editor/picking.h"

#include "core/log.h"
#include "platform/input.h"
#include "platform/window.h"
#include "scene/components.h"
#include "scene/editing.h"
#include "scene/serialization.h"

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
#include <cstring>
#include <string>
#include <vector>

namespace editor {
namespace {

// Version de GLSL declaree aux shaders internes d'ImGui. Elle doit correspondre au
// contexte cree par la plateforme - OpenGL 4.6 coeur.
constexpr const char* kGlslVersion = "#version 460 core";

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
    TranslationGizmo gizmo;
    std::string scenePath;
    // Dernier resultat d'enregistrement, affiche sous le bouton. Une sauvegarde
    // silencieuse laisse toujours un doute sur ce qui est parti sur le disque.
    std::string saveMessage;
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

void Editor::setScenePath(std::string path) {
    if (m_impl != nullptr) {
        m_impl->scenePath = std::move(path);
    }
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
    if (!isVisible()) {
        return false;
    }
    // Le gizmo compte autant qu'une fenetre : il vit DANS la vue, la ou ImGui considere
    // que la souris est libre. Sans cette ligne, tirer sur un bras ferait pivoter la
    // camera en meme temps, et l'objet suivrait un regard qui bouge.
    const bool gizmoBusy = m_impl->gizmo.isDragging() ||
                           m_impl->gizmo.hovered() != GizmoAxis::None;
    return ImGui::GetIO().WantCaptureMouse || gizmoBusy;
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

// Les reglages d'ambiance de la SCENE. Ils n'appartiennent a aucune entite : il n'y en a
// qu'un jeu, et ils n'ont pas de position.
//
// Ils meritent une fenetre parce qu'un reglage d'ambiance ne se choisit pas sur un nombre.
// L'exposition est le seul reglage du moteur dont je ne peux pas predire le bon reglage
// par le calcul : il depend de l'ecran, de la piece ou l'on joue, et du gout. Le mettre
// sous la main, en jeu, vaut mieux que de le deviner.
void drawEnvironmentPanel(scene::Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(320.0f, 210.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(312.0f, 452.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Ambiance")) {
        scene::Environment& environment = scene.environment();

        ImGui::TextUnformatted("Lumiere renvoyee par les surfaces");
        ImGui::ColorEdit3("ciel", &environment.skyColor.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::ColorEdit3("sol", &environment.groundColor.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::SliderFloat("intensite", &environment.intensity, 0.0f, 2.0f, "%.3f");
        ImGui::TextDisabled("Dose les recoins.");
        ImGui::TextDisabled("A zero, ce qu'aucune lampe n'atteint est noir.");

        ImGui::Separator();
        ImGui::TextUnformatted("Exposition");
        ImGui::SliderFloat("diaphragmes", &environment.exposureStops, -6.0f, 3.0f, "%.2f");
        ImGui::TextDisabled("Dose l'image entiere, lampes comprises.");
        ImGui::TextDisabled("Un diaphragme de moins divise par deux.");
    }
    ImGui::End();
}

void drawHierarchy(scene::Scene& scene, const scene::ResourceTable& resources,
                   const renderer::Camera& camera, Editor::Impl& impl);
void drawGizmo(scene::Scene& scene, const renderer::Camera& camera, scene::Entity selected,
               TranslationGizmo& gizmo);
void pickOnClick(scene::Scene& scene, const scene::ResourceTable& resources,
                 const renderer::Camera& camera, Editor::Impl& impl);

} // namespace

void Editor::draw(scene::Scene& scene, const scene::ResourceTable& resources,
                  const renderer::Camera& camera) {
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
        drawHierarchy(scene, resources, camera, impl);
        // Le gizmo D'ABORD : il doit avoir la priorite sur la designation, sans quoi
        // attraper un bras selectionnerait ce qu'il y a derriere.
        drawGizmo(scene, camera, impl.selected, impl.gizmo);
        pickOnClick(scene, resources, camera, impl);
    }

    ImGui::Render();
    // Le rendu se fait meme masque : ImGui exige qu'une image commencee soit terminee, et
    // une liste de commandes vide ne coute rien.
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

namespace {

// Couleurs des trois bras. L'association X rouge, Y vert, Z bleu est universelle dans les
// outils 3D : s'en ecarter desorienterait quiconque a deja touche a Blender ou Unity.
constexpr ImU32 kAxisColors[3] = {
    IM_COL32(220, 70, 70, 255),
    IM_COL32(90, 200, 90, 255),
    IM_COL32(80, 130, 230, 255),
};
constexpr ImU32 kHighlight = IM_COL32(255, 220, 90, 255);

void drawGizmo(scene::Scene& scene, const renderer::Camera& camera, scene::Entity selected,
               TranslationGizmo& gizmo) {
    if (selected == scene::kInvalidEntity) {
        return;
    }
    auto* transform = scene.registry().try_get<scene::Transform>(selected);
    if (transform == nullptr) {
        return;
    }

    // Le gizmo se place sur la position MONDE : un objet enfant se manipule la ou on le
    // voit, pas la ou son parent l'imagine.
    scene.updateWorldTransforms();
    const core::Mat4 world = scene.worldMatrix(selected);
    const core::Vec3 origin(world[3]);

    const ImGuiIO& io = ImGui::GetIO();
    TranslationGizmo::Frame frame;
    frame.viewProjection = camera.viewProjectionMatrix();
    frame.cameraPosition = camera.position();
    frame.mouse = core::Vec2(io.MousePos.x, io.MousePos.y);
    frame.viewport = core::Vec2(io.DisplaySize.x, io.DisplaySize.y);
    // Une fenetre survolee garde la souris : on ne veut pas attraper un bras a travers un
    // panneau. Mais une saisie EN COURS continue, meme si le curseur passe dessus.
    const bool overPanel = io.WantCaptureMouse && !gizmo.isDragging();
    frame.mousePressed = !overPanel && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    frame.mouseHeld = !overPanel && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    const core::Vec3 delta = gizmo.update(frame, origin);

    if (glm::dot(delta, delta) > 0.0f) {
        // Le deplacement est calcule dans le MONDE, le Transform vit dans le repere du
        // parent. Sans cette conversion, deplacer la poignee d'une porte mise a l'echelle
        // (0,9 ; 2,0 ; 0,08) l'enverrait douze fois trop loin sur un axe et huit fois trop
        // court sur un autre.
        core::Vec3 localDelta = delta;
        const auto* parent = scene.registry().try_get<scene::Parent>(selected);
        if (parent != nullptr && scene.isValid(parent->value)) {
            const core::Mat3 parentLinear(scene.worldMatrix(parent->value));
            localDelta = glm::inverse(parentLinear) * delta;
        }
        transform->position += localDelta;
    }

    // --- dessin ---------------------------------------------------------------------
    //
    // La liste d'arriere-plan : le gizmo se pose sur la scene mais passe SOUS les
    // panneaux, ce qui evite qu'un bras ne barre l'inspecteur.
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    core::Vec2 originScreen{0.0f, 0.0f};
    if (!worldToScreen(frame.viewProjection, origin, frame.viewport, originScreen)) {
        return;
    }
    const core::f32 arm = TranslationGizmo::armLength(frame.cameraPosition, origin);
    const GizmoAxis active =
        gizmo.isDragging() ? gizmo.dragged() : gizmo.hovered();

    core::u32 index = 0;
    for (const GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
        core::Vec2 tipScreen{0.0f, 0.0f};
        const core::Vec3 tip = origin + TranslationGizmo::axisDirection(axis) * arm;
        if (worldToScreen(frame.viewProjection, tip, frame.viewport, tipScreen)) {
            const bool lit = axis == active;
            draw->AddLine(ImVec2(originScreen.x, originScreen.y),
                          ImVec2(tipScreen.x, tipScreen.y),
                          lit ? kHighlight : kAxisColors[index], lit ? 4.0f : 2.5f);
            draw->AddCircleFilled(ImVec2(tipScreen.x, tipScreen.y), lit ? 6.0f : 4.0f,
                                  lit ? kHighlight : kAxisColors[index]);
        }
        ++index;
    }
    draw->AddCircleFilled(ImVec2(originScreen.x, originScreen.y), 3.0f,
                          IM_COL32(240, 240, 240, 255));
}

// Barre d'outils : creer, dupliquer, supprimer, enregistrer. Les quatre gestes qui
// separent un afficheur d'un editeur.
void drawToolbar(scene::Scene& scene, const scene::ResourceTable& resources,
                 const renderer::Camera& camera, Editor::Impl& impl) {
    if (ImGui::Button("Nouvelle")) {
        const scene::Entity created = scene.createEntity("nouvelle entite");
        // Devant la camera plutot qu'a l'origine : une entite creee hors de vue donne
        // l'impression que le bouton n'a rien fait.
        scene.registry().get<scene::Transform>(created).position =
            camera.position() + camera.forward() * 3.0f;
        impl.selected = created;
    }

    const bool hasSelection = impl.selected != scene::kInvalidEntity;
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Dupliquer")) {
        const scene::Entity copy = scene::duplicateEntity(scene, impl.selected);
        if (copy != scene::kInvalidEntity) {
            // On selectionne la copie : c'est elle qu'on va deplacer.
            impl.selected = copy;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Supprimer")) {
        scene::destroyEntityTree(scene, impl.selected);
        impl.selected = scene::kInvalidEntity;
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(impl.scenePath.empty());
    if (ImGui::Button("Enregistrer")) {
        const bool saved =
            scene::saveSceneToFile(scene, resources, impl.scenePath.c_str());
        // Un retour explicite : une sauvegarde silencieuse laisse toujours un doute sur
        // ce qui est parti sur le disque.
        impl.saveMessage = saved ? "scene enregistree" : "echec de l'enregistrement";
    }
    ImGui::EndDisabled();
    if (!impl.saveMessage.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", impl.saveMessage.c_str());
    }
}

// Un clic dans la vue designe l'objet vise. Trois cas s'en excluent, et chacun pour une
// raison differente.
void pickOnClick(scene::Scene& scene, const scene::ResourceTable& resources,
                 const renderer::Camera& camera, Editor::Impl& impl) {
    const ImGuiIO& io = ImGui::GetIO();
    // Sur un panneau : le clic appartient a l'interface.
    if (io.WantCaptureMouse) {
        return;
    }
    // Sur le gizmo : on manipule la selection courante, on n'en change pas.
    if (impl.gizmo.isDragging() || impl.gizmo.hovered() != GizmoAxis::None) {
        return;
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return;
    }

    const core::Mat4 inverse = glm::inverse(camera.viewProjectionMatrix());
    const Ray ray = screenRay(inverse, core::Vec2(io.MousePos.x, io.MousePos.y),
                              core::Vec2(io.DisplaySize.x, io.DisplaySize.y));

    // Les matrices monde doivent etre a jour : on teste contre la position affichee, pas
    // contre celle d'avant le dernier deplacement.
    scene.updateWorldTransforms();
    // Cliquer dans le vide DESELECTIONNE : c'est le geste attendu pour sortir d'une
    // selection, et le seul qui n'exige pas de viser autre chose.
    impl.selected = pickEntity(scene, resources, ray);
}

void drawHierarchy(scene::Scene& scene, const scene::ResourceTable& resources,
                   const renderer::Camera& camera, Editor::Impl& impl) {
    scene::Entity& selected = impl.selected;
    std::vector<scene::Entity>& scratch = impl.children;

    ImGui::SetNextWindowSize(ImVec2(280.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Hierarchie")) {
        drawToolbar(scene, resources, camera, impl);
        ImGui::Separator();

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
            // Un tampon fixe plutot que la liaison std::string d'ImGui : celle-ci vit
            // dans un fichier separe que le port vcpkg ne compile pas forcement, et
            // dependre de cela pour un champ de texte serait fragile.
            auto* name = scene.registry().try_get<scene::Name>(selected);
            if (name != nullptr) {
                char buffer[128];
                const std::size_t length =
                    std::min(name->value.size(), sizeof(buffer) - 1);
                std::memcpy(buffer, name->value.data(), length);
                buffer[length] = '\0';
                if (ImGui::InputText("nom", buffer, sizeof(buffer))) {
                    name->value = buffer;
                }
            } else {
                ImGui::TextUnformatted(displayName(scene, selected).c_str());
            }
            ImGui::Separator();

            drawComponentPanels(scene, resources, selected);
        }
    }
    ImGui::End();

    drawEnvironmentPanel(scene);
}

} // namespace

} // namespace editor
