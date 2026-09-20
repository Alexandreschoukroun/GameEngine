#include "physics/world.h"

#include "core/log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstdarg>
#include <vector>
#include <cstdio>

namespace physics {
namespace {

// --- Couches de collision ---------------------------------------------------------------
//
// Jolt separe les corps en deux classifications. Les COUCHES D'OBJET disent qui peut
// heurter qui : deux murs n'ont aucune raison d'etre testes l'un contre l'autre. Les
// COUCHES DE PHASE LARGE regroupent les corps dans l'arbre spatial : les statiques, qui ne
// bougent jamais, y sont separes des mobiles pour que leur arbre ne soit jamais reconstruit.
namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
// Le personnage a sa propre couche : c'est ce qui permet de dire qu'un objet TENU ne le
// heurte pas, alors qu'il continue de heurter les murs et les autres objets.
constexpr JPH::ObjectLayer kCharacter = 2;
constexpr JPH::ObjectLayer kHeld = 3;
constexpr JPH::ObjectLayer kCount = 4;
} // namespace Layers

namespace BroadPhase {
constexpr JPH::BroadPhaseLayer kStatic(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr core::u32 kCount = 2;
} // namespace BroadPhase

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override {
        // Statique contre statique : jamais, ils ne bougeront pas l'un vers l'autre.
        if (first == Layers::kStatic && second == Layers::kStatic) {
            return false;
        }
        // Objet tenu contre porteur : jamais non plus. Sans cette exception, ramener une
        // caisse contre soi la ferait pousser le joueur - et comme elle est pilotee a
        // vitesse imposee, elle le propulserait.
        const bool heldAgainstCharacter =
            (first == Layers::kHeld && second == Layers::kCharacter) ||
            (first == Layers::kCharacter && second == Layers::kHeld);
        return !heldAgainstCharacter;
    }
};

class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhase::kCount; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        // Tout ce qui n'est pas statique partage le meme arbre spatial : seuls les
        // statiques beneficient d'un arbre qui n'est jamais reconstruit.
        return layer == Layers::kStatic ? BroadPhase::kStatic : BroadPhase::kMoving;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhase::kStatic ? "statique" : "mobile";
    }
#endif
};

class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
        // Un statique n'a besoin d'etre teste que contre ce qui bouge.
        return layer != Layers::kStatic || broadPhase == BroadPhase::kMoving;
    }
};

// Jolt journalise par un rappel global. On le redirige vers nos logs plutot que vers la
// sortie standard, pour que tout passe par le meme canal.
void traceToEngineLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    core::logInfo(buffer);
}

JPH::Vec3 toJolt(const core::Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
JPH::Quat toJolt(const core::Quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
core::Vec3 fromJolt(JPH::Vec3 v) { return core::Vec3{v.GetX(), v.GetY(), v.GetZ()}; }
core::Quat fromJolt(JPH::Quat q) {
    // glm range le scalaire en premier, Jolt en dernier : une inversion silencieuse ici
    // ferait tourner tous les objets de travers.
    return core::Quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

} // namespace

struct World::Impl {
    // Dimensionnement : ce sont des plafonds, pas des allocations immediates. Genereux
    // pour une scene d'interieur, et a revoir quand une vraie scene existera.
    static constexpr JPH::uint kMaxBodies = 4096;
    static constexpr JPH::uint kMaxBodyPairs = 4096;
    static constexpr JPH::uint kMaxContactConstraints = 2048;

    JPH::TempAllocatorImpl tempAllocator{16 * 1024 * 1024};
    JPH::JobSystemThreadPool jobSystem{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                       static_cast<int>(std::thread::hardware_concurrency()) - 1};
    BroadPhaseLayerInterface broadPhaseLayers;
    ObjectVsBroadPhaseLayerFilter objectVsBroadPhase;
    ObjectLayerPairFilter objectPairs;
    JPH::PhysicsSystem system;

    // Les personnages sont comptes par reference chez Jolt : Ref les libere avec le monde.
    std::vector<JPH::Ref<JPH::CharacterVirtual>> characters;
    // Rayon et hauteur courante de chacun : une capsule Jolt ne se relit pas, il faut
    // donc garder de quoi la reconstruire.
    std::vector<core::f32> characterRadii;
    std::vector<core::f32> characterHeights;

    // Corps tenus par une charniere. Une liste suffit : il y en aura quelques dizaines
    // dans un niveau, pas des milliers.
    std::vector<BodyHandle> hingedBodies;
};

World::World() = default;
World::~World() { destroy(); }

bool World::create() {
    if (m_impl != nullptr) {
        return true;
    }

    JPH::RegisterDefaultAllocator();
    JPH::Trace = traceToEngineLog;
    // Jolt s'appuie sur une fabrique globale pour serialiser ses types. Elle doit exister
    // avant tout enregistrement.
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    m_impl = std::make_unique<Impl>();
    m_impl->system.Init(Impl::kMaxBodies, 0, Impl::kMaxBodyPairs, Impl::kMaxContactConstraints,
                        m_impl->broadPhaseLayers, m_impl->objectVsBroadPhase,
                        m_impl->objectPairs);
    m_impl->system.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    core::logInfo("monde physique cree");
    return true;
}

void World::destroy() {
    if (m_impl == nullptr) {
        return;
    }
    m_impl.reset();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

void World::step(core::f32 fixedDeltaSeconds) {
    if (m_impl == nullptr || fixedDeltaSeconds <= 0.0f) {
        return;
    }
    // Une seule sous-etape : le pas est deja fixe a 1/60 s, ce que Jolt considere comme
    // sa cadence de reference.
    m_impl->system.Update(fixedDeltaSeconds, 1, &m_impl->tempAllocator, &m_impl->jobSystem);

    // Les personnages avancent APRES la simulation, sur un monde deja resolu : ils voient
    // donc les objets a leur position finale, et non a celle du debut du pas.
    const JPH::Vec3 gravityVector = m_impl->system.GetGravity();
    for (const JPH::Ref<JPH::CharacterVirtual>& character : m_impl->characters) {
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        character->ExtendedUpdate(
            fixedDeltaSeconds, gravityVector, settings,
            m_impl->system.GetDefaultBroadPhaseLayerFilter(Layers::kCharacter),
            // Filtre de couche du PERSONNAGE : c'est lui qui exclut l'objet tenu.
            m_impl->system.GetDefaultLayerFilter(Layers::kCharacter), {}, {},
            m_impl->tempAllocator);
    }
}

BodyHandle World::addBox(const core::Vec3& position, const core::Quat& rotation,
                         const core::Vec3& halfExtents, bool isStatic,
                         core::f32 density) {
    if (m_impl == nullptr) {
        return kInvalidBody;
    }

    JPH::BodyInterface& bodies = m_impl->system.GetBodyInterface();
    JPH::BoxShapeSettings shapeSettings(toJolt(halfExtents));
    // La masse et l'inertie du corps decoulent de la forme : Jolt les calcule a partir du
    // volume et de cette densite. Inutile donc de donner une masse a la main.
    shapeSettings.SetDensity(density);
    const JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
    if (shape.HasError()) {
        core::logError("forme de collision invalide");
        return kInvalidBody;
    }

    const JPH::EMotionType motion =
        isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic;
    const JPH::ObjectLayer layer = isStatic ? Layers::kStatic : Layers::kMoving;

    JPH::BodyCreationSettings settings(shape.Get(), toJolt(position), toJolt(rotation), motion,
                                       layer);
    const JPH::BodyID id = bodies.CreateAndAddBody(settings, isStatic
                                                                 ? JPH::EActivation::DontActivate
                                                                 : JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        core::logError("creation de corps physique echouee");
        return kInvalidBody;
    }
    return id.GetIndexAndSequenceNumber();
}

core::Vec3 World::bodyPosition(BodyHandle body) const {
    if (m_impl == nullptr || body == kInvalidBody) {
        return core::Vec3{0.0f, 0.0f, 0.0f};
    }
    return fromJolt(m_impl->system.GetBodyInterface().GetPosition(JPH::BodyID(body)));
}

core::Quat World::bodyRotation(BodyHandle body) const {
    if (m_impl == nullptr || body == kInvalidBody) {
        return core::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return fromJolt(m_impl->system.GetBodyInterface().GetRotation(JPH::BodyID(body)));
}

void World::setBodyVelocity(BodyHandle body, const core::Vec3& linear) {
    if (m_impl == nullptr || body == kInvalidBody) {
        return;
    }
    JPH::BodyInterface& bodies = m_impl->system.GetBodyInterface();
    const JPH::BodyID id(body);
    // Un corps endormi ignore les changements de vitesse : Jolt desactive les objets
    // immobiles pour ne pas les simuler. Il faut donc le reveiller explicitement.
    bodies.ActivateBody(id);
    bodies.SetLinearVelocity(id, toJolt(linear));
}

core::Vec3 World::bodyVelocity(BodyHandle body) const {
    if (m_impl == nullptr || body == kInvalidBody) {
        return core::Vec3{0.0f, 0.0f, 0.0f};
    }
    return fromJolt(m_impl->system.GetBodyInterface().GetLinearVelocity(JPH::BodyID(body)));
}

BodyHandle World::addMesh(const core::Vec3& position, const core::Quat& rotation,
                          const core::Vec3& scale, const core::Vec3* vertices,
                          core::u32 vertexCount, const core::u32* indices,
                          core::u32 indexCount) {
    if (m_impl == nullptr || vertices == nullptr || indices == nullptr) {
        return kInvalidBody;
    }
    // Trois indices par triangle : un reste signale une geometrie tronquee, qu'il vaut
    // mieux refuser que d'interpreter a moitie.
    if (vertexCount == 0 || indexCount < 3 || indexCount % 3 != 0) {
        core::logError("collision de maillage : geometrie invalide");
        return kInvalidBody;
    }
    if (scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f) {
        // Une echelle negative retourne les triangles : les faces regarderaient vers
        // l'interieur, et le personnage traverserait le decor sans rien heurter.
        core::logError("collision de maillage : echelle nulle ou negative");
        return kInvalidBody;
    }

    JPH::VertexList joltVertices;
    joltVertices.reserve(vertexCount);
    for (core::u32 i = 0; i < vertexCount; ++i) {
        joltVertices.push_back(
            JPH::Float3(vertices[i].x, vertices[i].y, vertices[i].z));
    }

    JPH::IndexedTriangleList triangles;
    triangles.reserve(indexCount / 3);
    for (core::u32 i = 0; i + 2 < indexCount; i += 3) {
        if (indices[i] >= vertexCount || indices[i + 1] >= vertexCount ||
            indices[i + 2] >= vertexCount) {
            core::logError("collision de maillage : indice hors des sommets");
            return kInvalidBody;
        }
        triangles.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2]));
    }

    JPH::MeshShapeSettings shapeSettings(std::move(joltVertices), std::move(triangles));
    // Jolt construit ici un arbre sur les triangles : c'est ce qui rend le test de
    // collision logarithmique et non proportionnel au nombre de faces.
    JPH::ShapeSettings::ShapeResult shape = shapeSettings.Create();
    if (shape.HasError()) {
        core::logError("collision de maillage : construction refusee par Jolt");
        return kInvalidBody;
    }

    JPH::ShapeRefC finalShape = shape.Get();
    // L'echelle s'applique par une forme enveloppante plutot qu'en deformant les sommets :
    // deux objets a des echelles differentes partagent ainsi le meme arbre de triangles.
    if (scale.x != 1.0f || scale.y != 1.0f || scale.z != 1.0f) {
        JPH::ScaledShapeSettings scaledSettings(finalShape, toJolt(scale));
        JPH::ShapeSettings::ShapeResult scaled = scaledSettings.Create();
        if (scaled.HasError()) {
            core::logError("collision de maillage : mise a l'echelle refusee");
            return kInvalidBody;
        }
        finalShape = scaled.Get();
    }

    JPH::BodyInterface& bodies = m_impl->system.GetBodyInterface();
    JPH::BodyCreationSettings settings(finalShape, toJolt(position), toJolt(rotation),
                                       JPH::EMotionType::Static, Layers::kStatic);
    const JPH::BodyID id = bodies.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    if (id.IsInvalid()) {
        core::logError("collision de maillage : creation de corps echouee");
        return kInvalidBody;
    }
    return id.GetIndexAndSequenceNumber();
}

core::Vec3 World::bodyAngularVelocity(BodyHandle body) const {
    if (m_impl == nullptr || body == kInvalidBody) {
        return core::Vec3{0.0f, 0.0f, 0.0f};
    }
    return fromJolt(m_impl->system.GetBodyInterface().GetAngularVelocity(JPH::BodyID(body)));
}

void World::applyImpulseAtPoint(BodyHandle body, const core::Vec3& impulse,
                                const core::Vec3& point) {
    if (m_impl == nullptr || body == kInvalidBody) {
        return;
    }
    JPH::BodyInterface& bodies = m_impl->system.GetBodyInterface();
    const JPH::BodyID id(body);
    bodies.ActivateBody(id);
    bodies.AddImpulse(id, toJolt(impulse), toJolt(point));
}

bool World::addHinge(BodyHandle body, const core::Vec3& anchorPoint, const core::Vec3& axis,
                     core::f32 minAngle, core::f32 maxAngle, core::f32 friction) {
    if (m_impl == nullptr || body == kInvalidBody) {
        return false;
    }

    JPH::BodyLockWrite lock(m_impl->system.GetBodyLockInterface(), JPH::BodyID(body));
    if (!lock.Succeeded()) {
        core::logError("charniere : corps introuvable");
        return false;
    }

    JPH::HingeConstraintSettings settings;
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = settings.mPoint2 = toJolt(anchorPoint);
    settings.mHingeAxis1 = settings.mHingeAxis2 = toJolt(glm::normalize(axis));
    // L'axe normal sert de reference pour mesurer l'angle : il doit etre perpendiculaire
    // a l'axe de rotation, sinon les butees n'ont aucun sens.
    const JPH::Vec3 normalAxis = toJolt(glm::normalize(axis)).GetNormalizedPerpendicular();
    settings.mNormalAxis1 = settings.mNormalAxis2 = normalAxis;
    settings.mLimitsMin = minAngle;
    settings.mLimitsMax = maxAngle;
    // Un peu de friction : sans elle, une porte poussee tournerait indefiniment comme une
    // porte de saloon sans gonds.
    settings.mMaxFrictionTorque = friction;

    // Deuxieme corps : le monde lui-meme. La porte est accrochee a rien, donc a tout.
    JPH::Constraint* constraint = settings.Create(JPH::Body::sFixedToWorld, lock.GetBody());
    m_impl->system.AddConstraint(constraint);
    m_impl->hingedBodies.push_back(body);
    return true;
}

bool World::isBodyHinged(BodyHandle body) const {
    if (m_impl == nullptr) {
        return false;
    }
    return std::find(m_impl->hingedBodies.begin(), m_impl->hingedBodies.end(), body) !=
           m_impl->hingedBodies.end();
}

void World::setBodyHeld(BodyHandle body, bool held) {
    if (m_impl == nullptr || body == kInvalidBody) {
        return;
    }
    // Changer de couche de collision suffit : l'objet garde sa masse, sa forme et sa
    // vitesse, il cesse simplement d'exister pour le porteur.
    m_impl->system.GetBodyInterface().SetObjectLayer(
        JPH::BodyID(body), held ? Layers::kHeld : Layers::kMoving);
}

bool World::isBodyDynamic(BodyHandle body) const {
    if (m_impl == nullptr || body == kInvalidBody) {
        return false;
    }
    return m_impl->system.GetBodyInterface().GetMotionType(JPH::BodyID(body)) ==
           JPH::EMotionType::Dynamic;
}

RayHit World::raycast(const core::Vec3& origin, const core::Vec3& direction,
                      core::f32 maxDistance) const {
    RayHit result;
    if (m_impl == nullptr || maxDistance <= 0.0f) {
        return result;
    }

    // La direction porte la longueur : Jolt exprime la fraction du trajet parcourue, et
    // non une distance absolue.
    const JPH::Vec3 ray = toJolt(glm::normalize(direction)) * maxDistance;
    const JPH::RRayCast cast{toJolt(origin), ray};

    JPH::RayCastResult closest;
    if (!m_impl->system.GetNarrowPhaseQuery().CastRay(cast, closest)) {
        return result;
    }

    result.hit = true;
    result.body = closest.mBodyID.GetIndexAndSequenceNumber();
    result.distance = closest.mFraction * maxDistance;
    result.point = origin + glm::normalize(direction) * result.distance;

    // La normale demande d'interroger le corps touche : elle depend de la face heurtee,
    // que seule la forme connait.
    JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), closest.mBodyID);
    if (lock.Succeeded()) {
        result.normal =
            fromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(closest.mSubShapeID2,
                                                               toJolt(result.point)));
    }
    return result;
}

namespace {

JPH::CharacterVirtual* characterAt(const std::vector<JPH::Ref<JPH::CharacterVirtual>>& list,
                                   CharacterHandle handle) {
    return handle < list.size() ? list[handle].GetPtr() : nullptr;
}

} // namespace

namespace {

// Capsule d'un personnage, posee sur ses pieds.
//
// Une capsule et non une boite : elle glisse le long des murs et des coins au lieu de s'y
// accrocher. La demi-hauteur exclut les deux calottes spheriques, et le decalage vers le
// haut fait que la position du personnage designe ses PIEDS - ce qui rend
// l'accroupissement naturel, puisque la capsule raccourcit par le haut.
JPH::Ref<JPH::Shape> makeCharacterShape(core::f32 radius, core::f32 height) {
    const core::f32 halfHeight = height * 0.5f - radius;
    return JPH::RotatedTranslatedShapeSettings(
               JPH::Vec3(0.0f, halfHeight + radius, 0.0f), JPH::Quat::sIdentity(),
               new JPH::CapsuleShape(halfHeight, radius))
        .Create()
        .Get();
}

} // namespace

CharacterHandle World::addCharacter(const core::Vec3& feetPosition, core::f32 radius,
                                    core::f32 height) {
    if (m_impl == nullptr || height <= 2.0f * radius) {
        core::logError("personnage : hauteur incompatible avec le rayon");
        return kInvalidCharacter;
    }

    const JPH::Ref<JPH::Shape> capsule = makeCharacterShape(radius, height);

    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = capsule;
    // Au-dela de 46 degres, on glisse : c'est ce qui empeche de gravir un mur en le
    // longeant, defaut classique des controleurs maison.
    settings->mMaxSlopeAngle = JPH::DegreesToRadians(46.0f);
    // Plan de support : les contacts sous cette hauteur comptent comme du sol. Sans lui,
    // un contact a mi-hauteur de la capsule ferait croire au personnage qu'il est pose.
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);

    m_impl->characters.push_back(new JPH::CharacterVirtual(
        settings, toJolt(feetPosition), JPH::Quat::sIdentity(), &m_impl->system));
    // Le rayon est conserve : redimensionner la capsule demande de la reconstruire, et
    // une capsule ne se relit pas.
    m_impl->characterRadii.push_back(radius);
    m_impl->characterHeights.push_back(height);
    return static_cast<CharacterHandle>(m_impl->characters.size() - 1);
}

bool World::setCharacterHeight(CharacterHandle character, core::f32 height) {
    if (m_impl == nullptr || character >= m_impl->characters.size()) {
        return false;
    }
    const core::f32 radius = m_impl->characterRadii[character];
    if (height <= 2.0f * radius) {
        return false;
    }
    JPH::CharacterVirtual* c = m_impl->characters[character].GetPtr();

    // Jolt teste la nouvelle forme contre le decor et REFUSE si elle y penetrerait trop.
    // C'est exactement le test "puis-je me relever ici ?", et il vaut mieux que le notre :
    // il consulte la geometrie reelle plutot qu'un rayon qui pourrait passer a cote.
    const bool accepted = c->SetShape(
        makeCharacterShape(radius, height), 0.02f,
        m_impl->system.GetDefaultBroadPhaseLayerFilter(Layers::kCharacter),
        m_impl->system.GetDefaultLayerFilter(Layers::kCharacter), {}, {},
        m_impl->tempAllocator);
    if (accepted) {
        m_impl->characterHeights[character] = height;
    }
    return accepted;
}

core::f32 World::characterHeight(CharacterHandle character) const {
    if (m_impl == nullptr || character >= m_impl->characterHeights.size()) {
        return 0.0f;
    }
    return m_impl->characterHeights[character];
}

void World::setCharacterVelocity(CharacterHandle character, const core::Vec3& velocity) {
    if (m_impl == nullptr) {
        return;
    }
    if (JPH::CharacterVirtual* c = characterAt(m_impl->characters, character); c != nullptr) {
        c->SetLinearVelocity(toJolt(velocity));
    }
}

core::Vec3 World::characterVelocity(CharacterHandle character) const {
    if (m_impl == nullptr) {
        return core::Vec3{0.0f, 0.0f, 0.0f};
    }
    const JPH::CharacterVirtual* c = characterAt(m_impl->characters, character);
    return c != nullptr ? fromJolt(c->GetLinearVelocity()) : core::Vec3{0.0f, 0.0f, 0.0f};
}

core::Vec3 World::characterPosition(CharacterHandle character) const {
    if (m_impl == nullptr) {
        return core::Vec3{0.0f, 0.0f, 0.0f};
    }
    const JPH::CharacterVirtual* c = characterAt(m_impl->characters, character);
    return c != nullptr ? fromJolt(JPH::Vec3(c->GetPosition())) : core::Vec3{0.0f, 0.0f, 0.0f};
}

bool World::characterOnGround(CharacterHandle character) const {
    if (m_impl == nullptr) {
        return false;
    }
    const JPH::CharacterVirtual* c = characterAt(m_impl->characters, character);
    return c != nullptr && c->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

core::Vec3 World::gravity() const {
    return m_impl != nullptr ? fromJolt(m_impl->system.GetGravity())
                             : core::Vec3{0.0f, -9.81f, 0.0f};
}

} // namespace physics
