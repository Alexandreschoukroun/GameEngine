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
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

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
constexpr JPH::ObjectLayer kCount = 2;
} // namespace Layers

namespace BroadPhase {
constexpr JPH::BroadPhaseLayer kStatic(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr core::u32 kCount = 2;
} // namespace BroadPhase

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override {
        // Statique contre statique : jamais. Rien d'autre a exclure pour l'instant.
        return first == Layers::kMoving || second == Layers::kMoving;
    }
};

class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhase::kCount; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
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
        return layer == Layers::kMoving || broadPhase == BroadPhase::kMoving;
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
        character->ExtendedUpdate(fixedDeltaSeconds, gravityVector, settings,
                                  m_impl->system.GetDefaultBroadPhaseLayerFilter(Layers::kMoving),
                                  m_impl->system.GetDefaultLayerFilter(Layers::kMoving), {}, {},
                                  m_impl->tempAllocator);
    }
}

BodyHandle World::addBox(const core::Vec3& position, const core::Quat& rotation,
                         const core::Vec3& halfExtents, bool isStatic) {
    if (m_impl == nullptr) {
        return kInvalidBody;
    }

    JPH::BodyInterface& bodies = m_impl->system.GetBodyInterface();
    const JPH::BoxShapeSettings shapeSettings(toJolt(halfExtents));
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

namespace {

JPH::CharacterVirtual* characterAt(const std::vector<JPH::Ref<JPH::CharacterVirtual>>& list,
                                   CharacterHandle handle) {
    return handle < list.size() ? list[handle].GetPtr() : nullptr;
}

} // namespace

CharacterHandle World::addCharacter(const core::Vec3& feetPosition, core::f32 radius,
                                    core::f32 height) {
    if (m_impl == nullptr || height <= 2.0f * radius) {
        core::logError("personnage : hauteur incompatible avec le rayon");
        return kInvalidCharacter;
    }

    // Une capsule, et non une boite : elle glisse le long des murs et des coins au lieu de
    // s'y accrocher. La demi-hauteur exclut les deux calottes spheriques.
    const core::f32 halfHeight = height * 0.5f - radius;
    const JPH::Ref<JPH::Shape> capsule =
        JPH::RotatedTranslatedShapeSettings(
            // Decalage vers le haut : la position du personnage designe ses pieds, alors
            // qu'une capsule est centree sur son milieu.
            JPH::Vec3(0.0f, halfHeight + radius, 0.0f), JPH::Quat::sIdentity(),
            new JPH::CapsuleShape(halfHeight, radius))
            .Create()
            .Get();

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
    return static_cast<CharacterHandle>(m_impl->characters.size() - 1);
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
