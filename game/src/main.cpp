#include "assets/image.h"
#include "audio/engine.h"
#include "audio/tension.h"
#include "assets/mesh_data.h"
#include "core/math.h"
#include "core/log.h"
#include "core/types.h"
#include "physics/world.h"
#include "platform/application.h"
#include "renderer/camera.h"
#include "renderer/deferred_renderer.h"
#include "renderer/flashlight.h"
#include "renderer/light.h"
#include "rhi/device.h"
#include "rhi/mesh.h"
#include "rhi/texture.h"
#include "platform/paths.h"
#include "scene/audio_sync.h"
#include "scene/footsteps.h"
#include "scene/resource_table.h"
#include "scene/physics_sync.h"
#include "scene/scene.h"
#include "scene/sector_graph.h"
#include "scene/serialization.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr const char* kModelPath = "models/suzanne/Suzanne.gltf";
constexpr const char* kBaseColorPath = "models/suzanne/Suzanne_BaseColor.png";
constexpr const char* kMetallicRoughnessPath = "models/suzanne/Suzanne_MetallicRoughness.png";
constexpr const char* kScenePath = "scenes/demo.json";

constexpr core::f32 kLookSensitivity = 0.0022f; // radians par pixel de souris
constexpr core::f32 kWalkSpeed = 3.0f;          // metres par seconde
constexpr core::f32 kSprintSpeed = 5.2f;
constexpr core::f32 kJumpSpeed = 4.2f;

// Gabarit du personnage : 1,80 m debout, 0,35 m de rayon. Les yeux sont 15 cm sous le
// sommet du crane, comme chez un humain.
constexpr core::f32 kPlayerHeight = 1.8f;
constexpr core::f32 kPlayerRadius = 0.35f;
constexpr core::f32 kEyeHeight = 1.65f;
constexpr core::Vec3 kSpawnPosition{0.0f, -1.2f, 3.0f};

// --- Saisie d'objets --------------------------------------------------------------------
constexpr core::f32 kGrabRange = 2.6f;     // portee du bras, en metres
constexpr core::f32 kHoldDistance = 1.4f;  // ou l'objet flotte devant les yeux
// Nervosite du maintien : plus c'est eleve, plus l'objet colle a la main. Trop eleve, il
// traverserait les obstacles en une seule etape de simulation.
constexpr core::f32 kHoldStiffness = 12.0f;
constexpr core::f32 kMaxHoldSpeed = 7.0f;
// Au-dela, l'objet est arrache : il s'est coince dans un mur ou derriere une porte, et le
// garder reviendrait a le faire passer au travers.
constexpr core::f32 kBreakDistance = 2.6f;
// Impulsion transmise aux corps qu'on bouscule. Un controleur virtuel ne pousse rien tout
// seul : il faut le lui apprendre.
constexpr core::f32 kPushImpulse = 2.2f;
// Traction sur une porte. La main n'est pas un moteur mais un RESSORT AMORTI : elle tire
// d'autant plus fort que la porte est loin de la ou on la veut (raideur), et elle freine
// d'autant plus que la porte va vite (amortissement).
//
// Sans le terme d'amortissement, une force repetee chaque pas accelere le battant sans
// fin : c'est ce qui donnait une porte qui part en tournoyant. Les deux valeurs sont
// choisies proches de l'amortissement critique pour la masse effective au point saisi
// (environ 73 kg) : la porte rejoint la main sans osciller ni depasser.
constexpr core::f32 kDoorStiffness = 400.0f; // N/m
constexpr core::f32 kDoorDamping = 340.0f;   // N.s/m
// Plafond de force : viser brusquement loin sur le cote ne doit pas faire claquer la
// porte contre sa butee.
constexpr core::f32 kMaxDoorForce = 600.0f;

// Le crepitement du foyer. Le fichier est cite ici, mais QUI l'emet et a quel volume est
// decrit dans la scene : l'entite "braise" porte une source audio, comme elle porte un
// maillage. Le nom logique "braises" fait le lien, exactement comme "suzanne" pour un
// maillage.
constexpr const char* kFireSound = "audio/braises.wav";
constexpr const char* kFireSoundName = "braises";
// Un souffle grave, place DERRIERE la porte : c'est lui qui rend l'occlusion audible.
// Porte fermee, il est etouffe ; on l'entrouvre, il se degage.
constexpr const char* kDroneSound = "audio/souffle.wav";
constexpr const char* kDroneSoundName = "souffle";
// Les pas. Le fichier joue depend de ce que le sol declare sous les pieds du joueur : la
// scene coupe le sol en deux matieres, pierre a l'ouest et bois a l'est.
constexpr const char* kStoneStepSound = "audio/pas_pierre.wav";
constexpr const char* kWoodStepSound = "audio/pas_bois.wav";
// Longueur d'une foulee. La cadence se regle par la DISTANCE, donc courir rapproche les
// pas sans qu'on ait rien d'autre a faire.
constexpr core::f32 kStrideLength = 0.85f;
constexpr core::f32 kStepVolume = 0.45f;
// Les trois couches de la musique de tension, jouees en permanence et melangees selon une
// seule variable.
constexpr const char* kTensionSounds[3] = {
    "audio/tension_calme.wav",
    "audio/tension_pouls.wav",
    "audio/tension_aigu.wav",
};
// PROVISOIRE : en attendant l'AI Director de M7, la tension monte dans le noir et retombe
// lampe allumee. C'est un pilote de demonstration, pas une regle de jeu - il sert a rendre
// le systeme audible, et il sera remplace par la proximite de l'antagoniste.
constexpr core::f32 kTensionRise = 0.09f;  // par seconde, lampe eteinte
constexpr core::f32 kTensionFall = 0.35f;  // par seconde, lampe allumee

// Piece fermee de 12 x 4 x 12 metres. Sans murs, le faisceau de la lampe partirait dans le
// vide et on ne verrait rien de son cone : le livrable du SPEC parle bien d'une PIECE
// eclairee par une lampe torche.
constexpr core::f32 kRoomHalfWidth = 6.0f;
constexpr core::f32 kRoomFloorY = -1.3f;
constexpr core::f32 kRoomCeilingY = 2.7f;
constexpr core::f32 kWallUvScale = 0.5f; // un carreau de damier par demi-metre

constexpr core::u32 kCheckerSize = 256;
constexpr core::u32 kCheckerSquare = 32;
constexpr core::u32 kCookieSize = 256;
constexpr core::u32 kCrateCount = 5;
constexpr core::f32 kCrateHalfSize = 0.3f;

// Nombre maximal de lumieres transmises au renderer en une frame : la torche plus celles
// de la scene.
constexpr std::size_t kMaxSceneLights = renderer::kMaxLights;

// Ajoute un quadrilatere a la piece. Les sommets sont donnes dans l'ordre anti-horaire vu
// depuis l'INTERIEUR : c'est de la que la camera regarde, et le culling eliminerait les
// faces prises a l'envers.
void addQuad(std::vector<rhi::Vertex>& vertices, std::vector<core::u32>& indices,
             const core::Vec3& a, const core::Vec3& b, const core::Vec3& c,
             const core::Vec3& d, const core::Vec3& normal, core::f32 uScale,
             core::f32 vScale) {
    const auto base = static_cast<core::u32>(vertices.size());
    const core::Vec3 n = normal;
    vertices.push_back({{a.x, a.y, a.z}, {n.x, n.y, n.z}, {0.0f, 0.0f}});
    vertices.push_back({{b.x, b.y, b.z}, {n.x, n.y, n.z}, {uScale, 0.0f}});
    vertices.push_back({{c.x, c.y, c.z}, {n.x, n.y, n.z}, {uScale, vScale}});
    vertices.push_back({{d.x, d.y, d.z}, {n.x, n.y, n.z}, {0.0f, vScale}});
    for (core::u32 index : {0u, 1u, 2u, 0u, 2u, 3u}) {
        indices.push_back(base + index);
    }
}

// Faisceau de lampe torche, en niveaux de gris : blanc = la lumiere passe, noir = elle est
// bloquee. Un disque parfait trahirait immediatement l'artifice, d'ou le point chaud
// decentre et les stries du reflecteur.
std::vector<core::u8> makeFlashlightCookie() {
    std::vector<core::u8> pixels(static_cast<std::size_t>(kCookieSize) * kCookieSize * 4);
    const core::f32 half = static_cast<core::f32>(kCookieSize) * 0.5f;

    for (core::u32 y = 0; y < kCookieSize; ++y) {
        for (core::u32 x = 0; x < kCookieSize; ++x) {
            // Centre du faisceau legerement decale : l'ampoule d'une vraie lampe n'est
            // jamais parfaitement alignee avec le reflecteur.
            const core::f32 dx = (static_cast<core::f32>(x) - half * 0.94f) / half;
            const core::f32 dy = (static_cast<core::f32>(y) - half * 1.04f) / half;
            const core::f32 distance = std::sqrt(dx * dx + dy * dy);

            // Bord doux : le faisceau s'eteint entre 0,55 et 1,0 du rayon.
            core::f32 intensity = 1.0f - glm::smoothstep(0.55f, 1.0f, distance);
            // Stries radiales du reflecteur, tres legeres.
            const core::f32 angle = std::atan2(dy, dx);
            intensity *= 0.90f + 0.10f * std::cos(angle * 7.0f);
            // Assombrissement general vers l'exterieur, pour un centre plus chaud.
            intensity *= 1.0f - 0.35f * distance;

            const auto value = static_cast<core::u8>(
                glm::clamp(intensity, 0.0f, 1.0f) * 255.0f);
            const std::size_t index = (static_cast<std::size_t>(y) * kCookieSize + x) * 4;
            pixels[index + 0] = value;
            pixels[index + 1] = value;
            pixels[index + 2] = value;
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

std::vector<core::u8> makeCheckerboard() {
    std::vector<core::u8> pixels(static_cast<std::size_t>(kCheckerSize) * kCheckerSize * 4);
    for (core::u32 y = 0; y < kCheckerSize; ++y) {
        for (core::u32 x = 0; x < kCheckerSize; ++x) {
            const bool light = ((x / kCheckerSquare) + (y / kCheckerSquare)) % 2 == 0;
            const std::size_t index = (static_cast<std::size_t>(y) * kCheckerSize + x) * 4;
            pixels[index + 0] = light ? 190 : 60;
            pixels[index + 1] = light ? 184 : 56;
            pixels[index + 2] = light ? 176 : 54;
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

// Les donnees du fichier sont neutres : c'est ici qu'elles prennent la forme attendue par
// le GPU. La couche assets ignore volontairement ce qu'est un sommet pour rhi.
std::vector<rhi::Vertex> toVertices(const assets::MeshData& meshData) {
    std::vector<rhi::Vertex> vertices(meshData.positions.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const core::Vec3& position = meshData.positions[i];
        const core::Vec3& normal = meshData.normals[i];
        const core::Vec2& uv = meshData.uvs[i];
        vertices[i] = rhi::Vertex{
            {position.x, position.y, position.z}, {normal.x, normal.y, normal.z}, {uv.x, uv.y}};
    }
    return vertices;
}

class HorrorGame final : public platform::Application {
public:
    using Application::Application;

protected:
    bool onInit() override {
        if (!m_device.create(window().glProcAddressLoader())) {
            return false;
        }

        const core::f32 aspect = static_cast<core::f32>(window().width()) /
                                 static_cast<core::f32>(window().height());
        m_camera.setPerspective(core::radians(60.0f), aspect, 0.05f, 100.0f);
        window().setRelativeMouseMode(true);

        if (!m_renderer.create(window().width(), window().height())) {
            return false;
        }
        m_flashlight.snapTo(m_camera);
        if (!loadModel() || !loadRoom()) {
            return false;
        }
        // Le maillage des caisses doit exister AVANT le chargement : la scene le reclame
        // par son nom, et un nom inconnu declencherait le repli sur "missing".
        if (!buildCrateMesh()) {
            return false;
        }
        registerResources();

        // La scene ne vient plus du code : elle est lue dans un fichier. Modifier
        // demo.json et relancer suffit a changer le niveau, sans recompiler.
        // L'audio ne conditionne pas le lancement : un poste sans carte son doit pouvoir
        // afficher le jeu. On signale et on continue, les sources resteront muettes.
        if (!m_audio.create()) {
            core::logWarn("le jeu demarre sans audio");
        } else {
            // Comme pour les maillages : enregistrer AVANT le chargement, sinon la scene
            // reclamerait un nom que la table ne connait pas encore.
            m_resources.addSound(kFireSoundName,
                                 m_audio.loadSound(platform::assetPath(kFireSound)));
            m_resources.addSound(kDroneSoundName,
                                 m_audio.loadSound(platform::assetPath(kDroneSound)));
            m_resources.addSound("pas_pierre",
                                 m_audio.loadSound(platform::assetPath(kStoneStepSound)));
            m_resources.addSound("pas_bois",
                                 m_audio.loadSound(platform::assetPath(kWoodStepSound)));

            std::array<audio::SoundHandle, 3> tension{};
            for (core::u32 i = 0; i < tension.size(); ++i) {
                tension[i] = m_audio.loadSound(platform::assetPath(kTensionSounds[i]));
            }
            m_tension.start(m_audio, tension);
        }

        if (!scene::loadSceneFromFile(m_scene, m_resources,
                                      platform::assetPath(kScenePath).c_str())) {
            return false;
        }
        m_statue = m_scene.findByName("statue");

        if (!m_physics.create()) {
            return false;
        }

        // Les corps physiques sont crees a partir des composants Collider lus dans le
        // fichier : decor statique et caisses dynamiques, decrits au meme endroit que le
        // reste de la scene.
        scene::createPhysicsBodies(m_scene, m_physics);

        m_player = m_physics.addCharacter(kSpawnPosition, kPlayerRadius, kPlayerHeight);
        if (m_player == physics::kInvalidCharacter) {
            return false;
        }
        m_camera.setPosition(kSpawnPosition + core::Vec3{0.0f, kEyeHeight, 0.0f});
        m_flashlight.snapTo(m_camera);

        // Les sources decrites dans la scene prennent vie. Le jeu ne sait plus quelle
        // entite sonne ni a quel volume : c'est ecrit dans le fichier, comme les
        // colliders et les lumieres avant lui.
        scene::startAudioSources(m_scene, m_resources, m_audio);
        m_footsteps.configure(kStrideLength, kStepVolume);
        return true;
    }

    // Une fois par frame : le regard suit la souris a la frequence de l'ecran.
    void onFrame(core::f64 frameDeltaSeconds) override {
        // Un deplacement souris est deja une quantite, pas un taux : il ne se multiplie
        // pas par le temps ecoule. Le rattrapage de la lampe, lui, en depend.
        m_camera.addRotation(input().mouseDeltaX() * kLookSensitivity,
                             -input().mouseDeltaY() * kLookSensitivity);
        m_flashlight.update(m_camera, frameDeltaSeconds);

        // L'oreille suit l'oeil. Sans cet appel, tourner la tete ne changerait rien a ce
        // qu'on entend - et c'est precisement par l'oreille qu'on localise une menace
        // qu'on ne voit pas.
        m_audio.setListener(m_camera.position(), m_camera.forward(),
                            core::Vec3{0.0f, 1.0f, 0.0f});
        // Les sources suivent leur entite. La braise est fille de la statue, qui tourne :
        // sans cette ligne, le crepitement resterait fige la ou la braise se trouvait au
        // chargement, pendant que la lueur s'en eloigne. L'oreille et l'oeil se
        // contrediraient.
        // La tension suit l'obscurite, faute d'antagoniste pour la piloter.
        const core::f32 frameDelta = static_cast<core::f32>(frameDeltaSeconds);
        m_tensionLevel += (m_flashlight.isEnabled() ? -kTensionFall : kTensionRise) * frameDelta;
        m_tensionLevel = std::clamp(m_tensionLevel, 0.0f, 1.0f);
        m_tension.setTension(m_tensionLevel);
        m_tension.update(m_audio, frameDelta);

        m_scene.updateWorldTransforms();
        scene::syncAudioSources(m_scene, m_audio);
        // Ce qui separe chaque source de l'oreille. C'est ici que la porte a charniere de
        // M4 prend tout son sens : l'entrouvrir laisse passer le son progressivement,
        // parce que son angle est une donnee simulee et non une animation.
        scene::updateAudioOcclusion(m_scene, m_physics, m_audio, m_camera.position());
        m_audio.update(frameDelta);

        // F5 ecrit la scene sur le disque. Deux appuis successifs produisent exactement
        // le meme fichier : c'est l'exigence de determinisme du SPEC.
        const bool f5Down = input().isKeyDown(platform::Key::F5);
        if (f5Down && !m_f5WasDown) {
            scene::saveSceneToFile(m_scene, m_resources,
                                   platform::assetPath("scenes/demo.json").c_str());
        }
        m_f5WasDown = f5Down;

        updateGrab();

        const bool fDown = input().isKeyDown(platform::Key::F);
        if (fDown && !m_fWasDown) {
            m_flashlight.toggle();
        }
        m_fWasDown = fDown;

        // Secteur courant : c'est cette information que le culling du rendu, l'occlusion
        // audio de M5 et l'ouie de l'IA de M7 consommeront. Pour l'instant, on l'annonce.
        updateCurrentSector();

        // Tab fait defiler les vues du G-buffer. On ne reagit qu'a l'instant ou la touche
        // s'enfonce : sinon la vue changerait soixante fois par seconde.
        const bool tabDown = input().isKeyDown(platform::Key::Tab);
        if (tabDown && !m_tabWasDown) {
            const auto next = (static_cast<core::i32>(m_renderer.debugView()) + 1) %
                              static_cast<core::i32>(renderer::DebugView::Count);
            m_renderer.setDebugView(static_cast<renderer::DebugView>(next));
        }
        m_tabWasDown = tabDown;
    }

    // A pas fixe : le deplacement est de la simulation, il doit etre deterministe.
    void onFixedUpdate(core::f64 fixedDeltaSeconds) override {
        using platform::Key;
        const auto delta = static_cast<core::f32>(fixedDeltaSeconds);

        // Direction voulue, a plat : on projette le regard sur le sol, sinon regarder ses
        // pieds ralentirait la marche.
        core::Vec3 forward = m_camera.forward();
        forward.y = 0.0f;
        core::Vec3 right = m_camera.right();
        right.y = 0.0f;

        core::Vec3 wish{0.0f, 0.0f, 0.0f};
        if (input().isKeyDown(Key::W)) {
            wish += forward;
        }
        if (input().isKeyDown(Key::S)) {
            wish -= forward;
        }
        if (input().isKeyDown(Key::D)) {
            wish += right;
        }
        if (input().isKeyDown(Key::A)) {
            wish -= right;
        }
        if (glm::dot(wish, wish) > 0.0f) {
            // Normaliser evite d'aller plus vite en diagonale.
            wish = glm::normalize(wish);
        }

        const core::f32 speed =
            input().isKeyDown(Key::LeftShift) ? kSprintSpeed : kWalkSpeed;
        const bool onGround = m_physics.characterOnGround(m_player);
        core::Vec3 velocity = m_physics.characterVelocity(m_player);

        // Au sol, la vitesse verticale est remise a zero : sans ca, la gravite
        // s'accumulerait indefiniment pendant qu'on marche, et le premier bord de marche
        // provoquerait une chute a grande vitesse.
        core::Vec3 newVelocity{wish.x * speed, onGround ? 0.0f : velocity.y, wish.z * speed};

        if (onGround && input().isKeyDown(Key::Space)) {
            newVelocity.y = kJumpSpeed;
        } else if (!onGround) {
            // La gravite est celle du monde physique, pas une constante recopiee ici : une
            // seule verite pour une seule information.
            newVelocity += m_physics.gravity() * delta;
        }

        m_physics.setCharacterVelocity(m_player, newVelocity);

        if (glm::dot(wish, wish) > 0.0f) {
            pushTouchedBodies();
        }

        // La statue tourne lentement sur elle-meme. Ses deux enfants suivent sans qu'on
        // touche a leur Transform : c'est toute la hierarchie en une ligne.
        if (m_scene.isValid(m_statue)) {
            m_statueAngle += 0.35f * delta;
            // On reconstruit la rotation depuis un angle plutot que de composer un
            // quaternion a chaque frame : multiplier mille petites rotations accumulerait
            // une derive numerique.
            m_scene.registry().get<scene::Transform>(m_statue).rotation =
                glm::angleAxis(m_statueAngle, core::Vec3{0.0f, 1.0f, 0.0f});
        }

        // La physique avance au meme rythme, et uniquement ici : elle n'est deterministe
        // qu'a pas constant.
        updateHeldBody(delta);
        stepPhysics(delta);

        // Les pas : la distance reellement parcourue depuis le pas fixe precedent, a
        // plat. On mesure le deplacement CONSTATE et non la vitesse demandee - pousser
        // contre un mur ne doit pas faire marcher sur place.
        const core::Vec3 feet = m_physics.characterPosition(m_player);
        const core::Vec3 stride{feet.x - m_lastFeet.x, 0.0f, feet.z - m_lastFeet.z};
        m_footsteps.update(m_scene, m_physics, m_resources, m_audio, feet,
                           m_physics.characterOnGround(m_player), glm::length(stride));
        m_lastFeet = feet;

        // Les yeux suivent le corps. Le sens compte : c'est la simulation qui decide ou se
        // trouve le joueur, la camera ne fait que la regarder.
        m_camera.setPosition(feet + core::Vec3{0.0f, kEyeHeight, 0.0f});
    }

    void onResize(core::u32 width, core::u32 height) override {
        if (width == 0 || height == 0) {
            return; // fenetre reduite : on ignore, sinon on divise par zero
        }
        m_camera.setAspect(static_cast<core::f32>(width) / static_cast<core::f32>(height));
        m_renderer.resize(width, height);
    }

    void onRender() override {
        // Deux systemes, au sens ECS : ils parcourent les entites possedant les composants
        // qui les interessent, et en tirent ce que le renderer attend.
        // Une seule passe recalcule toutes les matrices monde, parents avant enfants.
        // Les systemes qui suivent n'ont plus qu'a les lire.
        m_scene.updateWorldTransforms();

        m_drawItems.clear();
        for (auto [entity, world, mesh] :
             m_scene.registry().view<scene::WorldTransform, scene::MeshRenderer>().each()) {
            // La poignee se resout ici, une fois par objet et par frame : c'est une simple
            // indexation de tableau.
            const rhi::Mesh* meshResource = m_resources.mesh(mesh.mesh);
            if (meshResource == nullptr) {
                continue;
            }
            const core::Mat3 normalMatrix =
                glm::transpose(glm::inverse(core::Mat3(world.matrix)));
            m_drawItems.push_back(renderer::DrawItem{
                meshResource, m_resources.texture(mesh.baseColor),
                m_resources.texture(mesh.metallicRoughness), world.matrix, normalMatrix});
        }

        // La lampe torche en premier : c'est elle qui porte l'ombre, et le renderer retient
        // la premiere lumiere a ombre de la liste.
        m_lights.clear();
        m_lights.push_back(m_flashlight.light());
        for (auto [entity, world, source] :
             m_scene.registry().view<scene::WorldTransform, scene::LightSource>().each()) {
            if (m_lights.size() >= kMaxSceneLights) {
                break;
            }
            renderer::Light light;
            // La position vient de la matrice monde, donc de la hierarchie : une lampe
            // enfant d'une porte suit la porte, sans code supplementaire.
            light.position = core::Vec3(world.matrix[3]);
            light.direction = core::Vec3(world.matrix * core::Vec4{0.0f, 0.0f, -1.0f, 0.0f});
            light.color = source.color;
            light.intensity = source.intensity;
            light.type = source.type;
            light.innerAngleRadians = source.innerAngleRadians;
            light.outerAngleRadians = source.outerAngleRadians;
            light.range = source.range;
            light.castsShadow = source.castsShadow;
            m_lights.push_back(light);
        }

        m_renderer.render(m_device, m_camera, m_drawItems, m_lights, window().width(),
                          window().height());
    }

    // Le contexte GPU est encore vivant ici : c'est le seul endroit ou liberer ces objets.
    void onShutdown() override {
        m_tension.stop(m_audio);
        m_audio.destroy();
        m_physics.destroy();
        m_crateMesh.destroy();
        m_renderer.destroy();
        m_missingTexture.destroy();
        m_cookie.destroy();
        m_floorMaterial.destroy();
        m_floorBaseColor.destroy();
        m_floorMesh.destroy();
        m_modelMetallicRoughness.destroy();
        m_modelBaseColor.destroy();
        m_modelMesh.destroy();
    }

private:
    // La scene remplace les variables membres : chaque objet est une entite, decrite par
    // ses composants. C'est ce qui rendra le chargement depuis un fichier possible.
    // Les ressources recoivent un nom logique : c'est lui qu'ecrivent et relisent les
    // fichiers de scene, jamais une adresse memoire ni un chemin de disque.
    void registerResources() {
        m_resources.addMesh("piece", &m_floorMesh);
        m_resources.addTexture("damier", &m_floorBaseColor);
        m_resources.addTexture("mat_rugueux", &m_floorMaterial);
        m_resources.addMesh("suzanne", &m_modelMesh);
        m_resources.addTexture("suzanne_couleur", &m_modelBaseColor);
        m_resources.addTexture("suzanne_matiere", &m_modelMetallicRoughness);
        // Remplacement des ressources introuvables : un magenta franc, impossible a
        // confondre avec une texture legitime.
        m_resources.addTexture("missing", &m_missingTexture);
        m_resources.addMesh("caisse", &m_crateMesh);
    }

    // Cube de 1 m de cote, centre sur l'origine : la mise a l'echelle du Transform lui
    // donne sa taille finale.
    bool buildCrateMesh() {
        std::vector<rhi::Vertex> vertices;
        std::vector<core::u32> indices;
        const core::f32 h = 0.5f;
        addQuad(vertices, indices, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h},
                {0.0f, 0.0f, 1.0f}, 1.0f, 1.0f);
        addQuad(vertices, indices, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h},
                {0.0f, 0.0f, -1.0f}, 1.0f, 1.0f);
        addQuad(vertices, indices, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h},
                {1.0f, 0.0f, 0.0f}, 1.0f, 1.0f);
        addQuad(vertices, indices, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h},
                {-1.0f, 0.0f, 0.0f}, 1.0f, 1.0f);
        addQuad(vertices, indices, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h},
                {0.0f, 1.0f, 0.0f}, 1.0f, 1.0f);
        addQuad(vertices, indices, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h},
                {0.0f, -1.0f, 0.0f}, 1.0f, 1.0f);
        return m_crateMesh.create(vertices.data(), static_cast<core::u32>(vertices.size()),
                                  indices.data(), static_cast<core::u32>(indices.size()));
    }

    // La physique avance, puis les entites recopient la pose de leur corps. Le sens
    // compte : la simulation fait autorite sur la position d'un objet dynamique.
    void stepPhysics(core::f32 fixedDeltaSeconds) {
        m_physics.step(fixedDeltaSeconds);
        scene::syncTransformsFromPhysics(m_scene, m_physics);
    }

    // Saisie et maintien d'objets. L'objet tenu reste un corps dynamique ordinaire : il
    // heurte les murs, se coince dans une porte et repousse ce qu'il touche. C'est ce qui
    // distingue une manipulation physique d'un objet colle a l'ecran.
    void updateGrab() {
        // Maintien du clic plutot qu'une touche a basculer : on saisit, on glisse, on
        // relache. C'est le geste d'Amnesia, et il rend la manipulation continue - la
        // porte suit la main tant qu'on tient, et s'arrete des qu'on lache.
        const bool grabDown = input().isMouseButtonDown(platform::MouseButton::Left);
        const bool justPressed = grabDown && !m_useWasDown;
        const bool justReleased = !grabDown && m_useWasDown;
        m_useWasDown = grabDown;

        if (justReleased) {
            releaseHeldBody();
            return;
        }

        if (justPressed) {
            if (m_heldBody == physics::kInvalidBody) {
                // On vise depuis l'oeil, dans l'axe du regard : exactement ce que voit le
                // joueur, et non une zone approximative autour de lui.
                const physics::RayHit hit = m_physics.raycast(
                    m_camera.position(), m_camera.forward(), kGrabRange);
                if (hit.hit && m_physics.isBodyDynamic(hit.body)) {
                    m_heldBody = hit.body;
                    m_grabPoint = hit.point;
                    // Decalage du point saisi par rapport au centre du corps : c'est lui
                    // qui donne le bras de levier, donc la difference entre tirer sur une
                    // poignee et pousser au milieu d'un battant.
                    m_grabOffset = hit.point - m_physics.bodyPosition(hit.body);
                    // Une porte reste solide pour le joueur : la traverser en la tenant
                    // n'aurait aucun sens. Seuls les objets libres sont neutralises.
                    if (!m_physics.isBodyHinged(m_heldBody)) {
                        m_physics.setBodyHeld(m_heldBody, true);
                    }
                }
            }
        }

    }

    // Les forces exercees sur l'objet tenu sont de la SIMULATION : elles appartiennent au
    // pas fixe, pas a la frame. Appliquees par frame, elles dependraient de la vitesse de
    // la machine - une porte deux fois plus rapide sur un ecran a 144 Hz que sur un 60 Hz.
    // C'est exactement le defaut que le pas fixe de M0 existe pour empecher.
    void updateHeldBody(core::f32 fixedDeltaSeconds) {
        if (m_heldBody == physics::kInvalidBody) {
            return;
        }

        const core::Vec3 target =
            m_camera.position() + m_camera.forward() * kHoldDistance;

        // Une porte ne se manipule pas comme une caisse : elle est accrochee a ses gonds.
        // Lui imposer une vitesse se battrait contre la charniere ; on applique donc une
        // force au POINT saisi, et la rotation en decoule.
        if (m_physics.isBodyHinged(m_heldBody)) {
            pullHeldDoor(target, fixedDeltaSeconds);
            return;
        }

        const core::Vec3 toTarget = target - m_physics.bodyPosition(m_heldBody);

        // L'objet s'est coince : le ramener de force le ferait traverser l'obstacle.
        if (glm::length(toTarget) > kBreakDistance) {
            releaseHeldBody();
            return;
        }

        // Vitesse proportionnelle a l'ecart, plafonnee. C'est un ressort sans masse : il
        // n'oscille pas, et le plafond garantit qu'aucun pas de simulation ne franchit un
        // mur d'un bond.
        core::Vec3 velocity = toTarget * kHoldStiffness;
        const core::f32 speed = glm::length(velocity);
        if (speed > kMaxHoldSpeed) {
            velocity *= kMaxHoldSpeed / speed;
        }
        m_physics.setBodyVelocity(m_heldBody, velocity);
    }

    void pullHeldDoor(const core::Vec3& target, core::f32 fixedDeltaSeconds) {
        const core::Vec3 center = m_physics.bodyPosition(m_heldBody);
        // Le point saisi suit la porte : on le recalcule depuis le corps, sinon on
        // tirerait indefiniment sur une position que la porte a quittee.
        m_grabPoint = center + m_grabOffset;

        // Vitesse du point saisi. Un corps en rotation n'a pas une vitesse unique : elle
        // vaut omega x r, donc elle croit avec la distance a l'axe. C'est cette vitesse-la
        // que la main freine, pas celle du centre.
        const core::Vec3 lever = m_grabPoint - center;
        const core::Vec3 pointVelocity =
            glm::cross(m_physics.bodyAngularVelocity(m_heldBody), lever);

        // Ressort amorti : on tire vers la cible, on freine proportionnellement a la
        // vitesse. C'est ce second terme qui manquait et qui laissait la porte s'emballer.
        core::Vec3 force =
            (target - m_grabPoint) * kDoorStiffness - pointVelocity * kDoorDamping;
        const core::f32 strength = glm::length(force);
        if (strength > kMaxDoorForce) {
            force *= kMaxDoorForce / strength;
        }

        // Force x temps = impulsion. Multiplier par le pas rend le resultat independant
        // de la frequence de simulation.
        m_physics.applyImpulseAtPoint(m_heldBody, force * fixedDeltaSeconds, m_grabPoint);
    }

    void releaseHeldBody() {
        if (m_heldBody != physics::kInvalidBody) {
            if (!m_physics.isBodyHinged(m_heldBody)) {
                m_physics.setBodyHeld(m_heldBody, false);
            }
            m_heldBody = physics::kInvalidBody;
        }
    }

    // Un controleur virtuel detecte les corps dynamiques sans leur transmettre de force :
    // une caisse arreterait le joueur comme un mur. On la pousse donc explicitement.
    void pushTouchedBodies() {
        const core::Vec3 eye = m_camera.position();
        core::Vec3 direction = m_camera.forward();
        direction.y = 0.0f;
        if (glm::dot(direction, direction) <= 0.0f) {
            return;
        }
        direction = glm::normalize(direction);

        // Un rayon a hauteur de hanche, juste devant : de quoi detecter ce qu'on bouscule
        // en marchant, sans attraper ce qui est au-dessus ou derriere.
        const physics::RayHit hit =
            m_physics.raycast(eye - core::Vec3{0.0f, 0.8f, 0.0f}, direction, 0.75f);
        if (!hit.hit || hit.body == m_heldBody || !m_physics.isBodyDynamic(hit.body)) {
            return;
        }
        const core::Vec3 velocity = m_physics.bodyVelocity(hit.body);
        m_physics.setBodyVelocity(hit.body, velocity + direction * kPushImpulse);
    }

    void updateCurrentSector() {
        // Les matrices monde doivent etre a jour : un secteur peut etre enfant d'autre
        // chose. La passe est idempotente dans une meme frame grace au champ epoch.
        m_scene.updateWorldTransforms();

        const scene::Entity sector = scene::sectorAt(m_scene, m_camera.position());
        if (sector == m_currentSector) {
            return;
        }
        m_currentSector = sector;

        if (sector == scene::kInvalidEntity) {
            core::logInfo("hors de tout secteur");
            return;
        }

        const std::string& name = m_scene.registry().get<scene::Name>(sector).value;
        scene::reachableSectors(m_scene, sector, 1, m_reachable);
        // Un portail franchi : ce sont les secteurs que le rendu devra dessiner, et ceux
        // d'ou un son pourra parvenir sans traverser de mur.
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "secteur : %s (%zu atteignables a 1 portail)",
                      name.c_str(), m_reachable.size());
        core::logInfo(buffer);
    }

    bool loadModel() {
        assets::MeshData meshData;
        if (!assets::loadGltfMesh(platform::assetPath(kModelPath).c_str(), meshData)) {
            return false;
        }
        const std::vector<rhi::Vertex> vertices = toVertices(meshData);
        if (!m_modelMesh.create(vertices.data(), static_cast<core::u32>(vertices.size()),
                                meshData.indices.data(),
                                static_cast<core::u32>(meshData.indices.size()))) {
            return false;
        }

        assets::ImageData baseColor;
        assets::ImageData metallicRoughness;
        if (!assets::loadImage(platform::assetPath(kBaseColorPath).c_str(), baseColor) ||
            !assets::loadImage(platform::assetPath(kMetallicRoughnessPath).c_str(),
                               metallicRoughness)) {
            return false;
        }

        // La couleur de base est une couleur : le GPU doit la ramener en lineaire a chaque
        // lecture. La carte metallicite/rugosite contient des mesures : aucune conversion,
        // sinon les valeurs seraient faussees.
        return m_modelBaseColor.create(baseColor.width, baseColor.height,
                                       baseColor.pixels.data(),
                                       rhi::TextureFormat::SrgbColor) &&
               m_modelMetallicRoughness.create(metallicRoughness.width,
                                               metallicRoughness.height,
                                               metallicRoughness.pixels.data(),
                                               rhi::TextureFormat::LinearData);
    }

    bool loadRoom() {
        const core::f32 w = kRoomHalfWidth;
        const core::f32 floorY = kRoomFloorY;
        const core::f32 ceilY = kRoomCeilingY;
        const core::f32 uv = 2.0f * w * kWallUvScale;
        const core::f32 uvHeight = (ceilY - floorY) * kWallUvScale;

        std::vector<rhi::Vertex> vertices;
        std::vector<core::u32> indices;
        // Sol, plafond, puis les quatre murs. Toutes les normales pointent vers
        // l'interieur de la piece.
        addQuad(vertices, indices, {-w, floorY, -w}, {-w, floorY, w}, {w, floorY, w},
                {w, floorY, -w}, {0.0f, 1.0f, 0.0f}, uv, uv);
        addQuad(vertices, indices, {-w, ceilY, -w}, {w, ceilY, -w}, {w, ceilY, w},
                {-w, ceilY, w}, {0.0f, -1.0f, 0.0f}, uv, uv);
        addQuad(vertices, indices, {-w, floorY, -w}, {w, floorY, -w}, {w, ceilY, -w},
                {-w, ceilY, -w}, {0.0f, 0.0f, 1.0f}, uv, uvHeight);
        addQuad(vertices, indices, {w, floorY, w}, {-w, floorY, w}, {-w, ceilY, w},
                {w, ceilY, w}, {0.0f, 0.0f, -1.0f}, uv, uvHeight);
        addQuad(vertices, indices, {-w, floorY, w}, {-w, floorY, -w}, {-w, ceilY, -w},
                {-w, ceilY, w}, {1.0f, 0.0f, 0.0f}, uv, uvHeight);
        addQuad(vertices, indices, {w, floorY, -w}, {w, floorY, w}, {w, ceilY, w},
                {w, ceilY, -w}, {-1.0f, 0.0f, 0.0f}, uv, uvHeight);

        if (!m_floorMesh.create(vertices.data(), static_cast<core::u32>(vertices.size()),
                                indices.data(), static_cast<core::u32>(indices.size()))) {
            return false;
        }

        const std::vector<core::u8> pixels = makeCheckerboard();
        if (!m_floorBaseColor.create(kCheckerSize, kCheckerSize, pixels.data(),
                                     rhi::TextureFormat::SrgbColor)) {
            return false;
        }

        // Materiau constant en une texture de 1x1 pixel : convention glTF, le vert porte
        // la rugosite et le bleu la metallicite. Le shader ne fait aucune difference avec
        // une vraie carte, et on evite d'ajouter des parametres de matiere partout.
        const core::u8 material[4] = {0, 200, 0, 255}; // rugueux, non metallique
        if (!m_floorMaterial.create(1, 1, material, rhi::TextureFormat::LinearData)) {
            return false;
        }

        const core::u8 magenta[4] = {255, 0, 255, 255};
        if (!m_missingTexture.create(1, 1, magenta, rhi::TextureFormat::SrgbColor)) {
            return false;
        }

        // Le cookie module l'intensite de la lampe : ce sont des mesures, pas une couleur
        // a regarder, donc aucune conversion sRGB.
        const std::vector<core::u8> cookie = makeFlashlightCookie();
        if (!m_cookie.create(kCookieSize, kCookieSize, cookie.data(),
                             rhi::TextureFormat::LinearData)) {
            return false;
        }
        m_renderer.setSpotCookie(&m_cookie);
        return true;
    }

    rhi::Device m_device;
    renderer::DeferredRenderer m_renderer;
    renderer::Camera m_camera;

    rhi::Mesh m_modelMesh;
    rhi::Texture m_modelBaseColor;
    rhi::Texture m_modelMetallicRoughness;

    rhi::Mesh m_floorMesh;
    rhi::Texture m_floorBaseColor;
    rhi::Texture m_floorMaterial;
    rhi::Texture m_cookie;
    rhi::Texture m_missingTexture;

    renderer::Flashlight m_flashlight;
    audio::Engine m_audio;
    scene::FootstepPlayer m_footsteps;
    audio::TensionLayer m_tension;
    core::f32 m_tensionLevel = 0.0f;
    core::Vec3 m_lastFeet = kSpawnPosition;
    physics::World m_physics;
    physics::CharacterHandle m_player = physics::kInvalidCharacter;
    physics::BodyHandle m_heldBody = physics::kInvalidBody;
    core::Vec3 m_grabPoint{0.0f, 0.0f, 0.0f};
    core::Vec3 m_grabOffset{0.0f, 0.0f, 0.0f};
    bool m_useWasDown = false;
    rhi::Mesh m_crateMesh;

    core::f32 m_statueAngle = 0.0f;

    scene::Scene m_scene;
    scene::ResourceTable m_resources;
    scene::Entity m_statue = scene::kInvalidEntity;
    scene::Entity m_currentSector = scene::kInvalidEntity;
    std::vector<scene::Entity> m_reachable;
    // Reutilises d'une frame a l'autre : on vide sans liberer, donc aucune allocation dans
    // la boucle de frame une fois le regime etabli (regle 7 du SPEC).
    std::vector<renderer::DrawItem> m_drawItems;
    std::vector<renderer::Light> m_lights;

    bool m_tabWasDown = false;
    bool m_fWasDown = false;
    bool m_f5WasDown = false;
};

} // namespace

int main() {
    platform::ApplicationConfig config;
    config.title = "GameEngine -- M2";

    HorrorGame game(config);
    return game.run() ? 0 : 1;
}
