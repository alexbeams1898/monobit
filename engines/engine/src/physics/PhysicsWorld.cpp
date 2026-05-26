#include "physics/PhysicsWorld.h"

// Jolt includes — order matters; Jolt.h MUST come first.
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Math/Real.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>

namespace engine::physics
{

namespace
{

// ---- Layer setup ------------------------------------------------------
// Jolt uses two layer concepts:
//   * ObjectLayer (uint16): per-body. Drives object-vs-object filtering.
//   * BroadPhaseLayer (uint8): coarse bucket for broadphase.
// We define two object layers (Static, Moving) and two broadphase
// buckets (Static, Moving). Static <-> Static collisions are skipped
// (no point), Moving <-> {Static, Moving} are tested.

namespace Layers
{
constexpr JPH::ObjectLayer NON_MOVING = 0;
constexpr JPH::ObjectLayer MOVING     = 1;
constexpr JPH::ObjectLayer NUM_LAYERS = 2;
} // namespace Layers

namespace BroadPhaseLayers
{
constexpr JPH::BroadPhaseLayer NON_MOVING(0);
constexpr JPH::BroadPhaseLayer MOVING(1);
constexpr JPH::uint NUM_LAYERS = 2;
} // namespace BroadPhaseLayers

class BPLayerImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerImpl()
    {
        mObjToBroad[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjToBroad[Layers::MOVING]     = BroadPhaseLayers::MOVING;
    }
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::NUM_LAYERS;
    }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        return mObjToBroad[inLayer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer))
        {
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::NON_MOVING):
            return "NON_MOVING";
        case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::MOVING):
            return "MOVING";
        default:
            return "?";
        }
    }
#endif
private:
    JPH::BroadPhaseLayer mObjToBroad[Layers::NUM_LAYERS];
};

class ObjectVsBPFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
    {
        if (inLayer1 == Layers::NON_MOVING)
            return inLayer2 == BroadPhaseLayers::MOVING;
        return true; // MOVING collides with anything
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override
    {
        if (inLayer1 == Layers::NON_MOVING)
            return inLayer2 == Layers::MOVING;
        return true;
    }
};

// ---- Module state -----------------------------------------------------
bool                                    sInit = false;
std::unique_ptr<JPH::TempAllocatorImpl> sTempAlloc;
std::unique_ptr<JPH::JobSystemThreadPool> sJobSystem;
std::unique_ptr<BPLayerImpl>            sBPLayerImpl;
std::unique_ptr<ObjectVsBPFilterImpl>   sObjectVsBPFilter;
std::unique_ptr<ObjectLayerPairFilterImpl> sObjectLayerPairFilter;
std::unique_ptr<JPH::PhysicsSystem>     sPhysics;

// We track our own handle->body indirection so handle::id stays stable
// across Jolt internals. Static bodies use Jolt's BodyID; character
// controllers (CharacterVirtual) live outside the body system so we
// store them as raw pointers.
std::uint32_t sNextHandleId = 1;
struct HandleEntry
{
    enum class Kind { Static, Character };
    Kind                       kind = Kind::Static;
    JPH::BodyID                body_id;                  // Static only
    JPH::Ref<JPH::CharacterVirtual> character;           // Character only
    SurfaceTag                 tag = SurfaceTag::Unknown;
    std::string                debug_name;
};
std::unordered_map<std::uint32_t, HandleEntry> sHandles;

// Preloaded shape cache. Keyed by ShapeHandle::id; value is a Jolt
// shape ref-counted pointer. Shapes are built once at load time
// (slow: 100s of ms for large meshes in Debug) and reused for body
// creation each scene activation. Cleared at engine shutdown.
std::uint32_t sNextShapeId = 1;
std::unordered_map<std::uint32_t, JPH::RefConst<JPH::Shape>> sShapes;

BodyHandle allocHandle()
{
    return {sNextHandleId++};
}

ShapeHandle allocShapeHandle()
{
    return {sNextShapeId++};
}

// Trace / assert hooks (no-op in release; route to stderr in debug).
void TraceImpl(const char* fmt, ...)
{
    va_list args; va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    va_end(args);
}
#ifdef JPH_ENABLE_ASSERTS
bool AssertFailedImpl(const char* expr, const char* msg, const char* file, JPH::uint line)
{
    std::fprintf(stderr, "[jolt-assert] %s:%u  expr=%s  msg=%s\n",
                 file, line, expr, msg ? msg : "");
    return true; // breakpoint
}
#endif

inline JPH::Vec3 toJ(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
inline glm::vec3 fromJ(const JPH::Vec3& v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

} // namespace

bool initPhysics()
{
    if (sInit) return false;

    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceImpl;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = AssertFailedImpl;
#endif
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    sTempAlloc = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024); // 10 MB
    sJobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 2);
    sBPLayerImpl = std::make_unique<BPLayerImpl>();
    sObjectVsBPFilter = std::make_unique<ObjectVsBPFilterImpl>();
    sObjectLayerPairFilter = std::make_unique<ObjectLayerPairFilterImpl>();

    constexpr JPH::uint cMaxBodies = 4096;
    constexpr JPH::uint cNumBodyMutexes = 0;
    constexpr JPH::uint cMaxBodyPairs = 4096;
    constexpr JPH::uint cMaxContactConstraints = 2048;

    sPhysics = std::make_unique<JPH::PhysicsSystem>();
    sPhysics->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
                   *sBPLayerImpl, *sObjectVsBPFilter, *sObjectLayerPairFilter);
    sPhysics->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    sInit = true;
    return true;
}

void shutdownPhysics()
{
    if (!sInit) return;
    sHandles.clear();
    sShapes.clear();
    sPhysics.reset();
    sObjectLayerPairFilter.reset();
    sObjectVsBPFilter.reset();
    sBPLayerImpl.reset();
    sJobSystem.reset();
    sTempAlloc.reset();
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    sInit = false;
}

void updatePhysics(float dt)
{
    if (!sInit) return;
    // Jolt prefers fixed sub-steps for stability. ~60Hz internal step,
    // up to 4 collision sub-steps per frame call.
    constexpr int   cMaxSubSteps = 4;
    constexpr float cFixedDt = 1.0f / 60.0f;
    float remaining = dt;
    int   steps = 0;
    while (remaining > 0.0f && steps < cMaxSubSteps)
    {
        const float step = remaining > cFixedDt ? cFixedDt : remaining;
        sPhysics->Update(step, 1, sTempAlloc.get(), sJobSystem.get());
        // Update character controllers — they're separate from body sim.
        for (auto& kv : sHandles)
        {
            if (kv.second.kind == HandleEntry::Kind::Character && kv.second.character)
            {
                // Apply gravity to velocity before stepping. Jolt's
                // CharacterVirtual doesn't auto-accelerate the
                // capsule — the inGravity parameter to ExtendedUpdate
                // is for ground-body impulse reaction, not character
                // motion. Standard Jolt sample pattern: accumulate
                // gravity when in air; reset Y to 0 when supported
                // (else gravity keeps adding to it while standing).
                JPH::CharacterVirtual* ch = kv.second.character;
                JPH::Vec3 v = ch->GetLinearVelocity();
                using GS = JPH::CharacterVirtual::EGroundState;
                const bool on_ground = (ch->GetGroundState() == GS::OnGround);
                if (on_ground)
                {
                    // Reset vertical velocity when supported (else
                    // gravity accumulates while standing).
                    v = JPH::Vec3(v.GetX(), 0.0f, v.GetZ());
                }
                else
                {
                    // Free-fall: accumulate gravity into velocity.
                    v += sPhysics->GetGravity() * step;
                }
                ch->SetLinearVelocity(v);

                // EnhancedInternalEdgeRemoval is set at character
                // creation (see addCharacter); no per-frame toggle.
                JPH::CharacterVirtual::ExtendedUpdateSettings settings;
                // Disable stick-to-floor entirely. Every drop is
                // free-fall under gravity, no matter how small.
                // Jolt's default mStickToFloorStepDown is (0, -0.5, 0)
                // which makes the capsule snap onto any surface up to
                // 0.5m below — gamey, anti-physics. Real falls (curb,
                // stair edge, hill rim) should always trigger gravity.
                settings.mStickToFloorStepDown = JPH::Vec3::sZero();
                ch->ExtendedUpdate(
                    step,
                    sPhysics->GetGravity(),
                    settings,
                    sPhysics->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                    sPhysics->GetDefaultLayerFilter(Layers::MOVING),
                    {}, {}, *sTempAlloc);
            }
        }
        remaining -= step;
        ++steps;
    }
}

BodyHandle addStaticTrimesh(const std::vector<glm::vec3>& positions,
                            const std::vector<std::uint32_t>& indices,
                            SurfaceTag tag, const char* debug_name)
{
    if (!sInit || positions.empty() || indices.size() < 3 || indices.size() % 3 != 0)
        return kInvalidBody;

    JPH::VertexList verts;
    verts.reserve(positions.size());
    for (const auto& p : positions)
        verts.emplace_back(p.x, p.y, p.z);

    JPH::IndexedTriangleList tris;
    tris.reserve(indices.size() / 3);
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        tris.emplace_back(indices[i], indices[i + 1], indices[i + 2]);

    JPH::MeshShapeSettings mesh_settings(std::move(verts), std::move(tris));
    mesh_settings.SetEmbedded();
    const auto t0 = std::chrono::steady_clock::now();
    JPH::ShapeSettings::ShapeResult result = mesh_settings.Create();
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (ms > 5.0)
    {
        std::fprintf(stderr, "[physics] MeshShape::Create %.1fms for '%s' (%zu tris)\n",
                     ms, debug_name ? debug_name : "(unnamed)", indices.size() / 3);
    }
    if (result.HasError())
    {
        std::fprintf(stderr, "[physics] trimesh shape build failed: %s\n",
                     result.GetError().c_str());
        return kInvalidBody;
    }

    JPH::BodyCreationSettings body_settings(
        result.Get(), JPH::Vec3::sZero(), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    JPH::BodyInterface& bi = sPhysics->GetBodyInterface();
    JPH::BodyID body_id = bi.CreateAndAddBody(body_settings, JPH::EActivation::DontActivate);
    if (body_id.IsInvalid())
        return kInvalidBody;

    const BodyHandle h = allocHandle();
    HandleEntry entry;
    entry.kind = HandleEntry::Kind::Static;
    entry.body_id = body_id;
    entry.tag = tag;
    if (debug_name) entry.debug_name = debug_name;
    sHandles.emplace(h.id, std::move(entry));
    return h;
}

ShapeHandle createStaticTrimeshShape(const std::vector<glm::vec3>& positions,
                                     const std::vector<std::uint32_t>& indices)
{
    if (!sInit || positions.empty() || indices.size() < 3 || indices.size() % 3 != 0)
        return kInvalidShape;

    JPH::VertexList verts;
    verts.reserve(positions.size());
    for (const auto& p : positions)
        verts.emplace_back(p.x, p.y, p.z);

    JPH::IndexedTriangleList tris;
    tris.reserve(indices.size() / 3);
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        tris.emplace_back(indices[i], indices[i + 1], indices[i + 2]);

    JPH::MeshShapeSettings mesh_settings(std::move(verts), std::move(tris));
    mesh_settings.SetEmbedded();
    const auto t0 = std::chrono::steady_clock::now();
    JPH::ShapeSettings::ShapeResult result = mesh_settings.Create();
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (ms > 5.0)
    {
        std::fprintf(stderr, "[physics] preload MeshShape::Create %.1fms (%zu tris)\n",
                     ms, indices.size() / 3);
    }
    if (result.HasError())
    {
        std::fprintf(stderr, "[physics] trimesh shape preload failed: %s\n",
                     result.GetError().c_str());
        return kInvalidShape;
    }

    const ShapeHandle sh = allocShapeHandle();
    sShapes.emplace(sh.id, result.Get());
    return sh;
}

BodyHandle addStaticBodyFromShape(ShapeHandle shape, SurfaceTag tag,
                                  const char* debug_name)
{
    if (!sInit || shape == kInvalidShape) return kInvalidBody;
    auto it = sShapes.find(shape.id);
    if (it == sShapes.end()) return kInvalidBody;

    JPH::BodyCreationSettings body_settings(
        it->second, JPH::Vec3::sZero(), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    JPH::BodyInterface& bi = sPhysics->GetBodyInterface();
    JPH::BodyID body_id = bi.CreateAndAddBody(body_settings, JPH::EActivation::DontActivate);
    if (body_id.IsInvalid()) return kInvalidBody;

    const BodyHandle h = allocHandle();
    HandleEntry entry;
    entry.kind = HandleEntry::Kind::Static;
    entry.body_id = body_id;
    entry.tag = tag;
    if (debug_name) entry.debug_name = debug_name;
    sHandles.emplace(h.id, std::move(entry));
    return h;
}

BodyHandle addStaticBox(const glm::vec3& center, const glm::vec3& half_extents,
                        SurfaceTag tag, const char* debug_name)
{
    if (!sInit || half_extents.x <= 0.0f || half_extents.y <= 0.0f || half_extents.z <= 0.0f)
        return kInvalidBody;

    // BoxShape requires half-extents >= a convex-radius (Jolt default 0.05).
    // Clamp tiny boxes up so very small step risers don't fail to build.
    constexpr float kMinHalf = 0.06f;
    JPH::Vec3 he(std::max(half_extents.x, kMinHalf),
                 std::max(half_extents.y, kMinHalf),
                 std::max(half_extents.z, kMinHalf));
    JPH::Ref<JPH::Shape> shape = new JPH::BoxShape(he);
    JPH::BodyCreationSettings body_settings(
        shape, toJ(center), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    JPH::BodyInterface& bi = sPhysics->GetBodyInterface();
    JPH::BodyID body_id = bi.CreateAndAddBody(body_settings, JPH::EActivation::DontActivate);
    if (body_id.IsInvalid())
        return kInvalidBody;

    const BodyHandle h = allocHandle();
    HandleEntry entry;
    entry.kind = HandleEntry::Kind::Static;
    entry.body_id = body_id;
    entry.tag = tag;
    if (debug_name) entry.debug_name = debug_name;
    sHandles.emplace(h.id, std::move(entry));
    return h;
}

const char* bodyDebugName(BodyHandle body)
{
    auto it = sHandles.find(body.id);
    if (it == sHandles.end()) return "";
    return it->second.debug_name.c_str();
}

void removeBody(BodyHandle body)
{
    if (!sInit || body.id == 0) return;
    auto it = sHandles.find(body.id);
    if (it == sHandles.end()) return;
    if (it->second.kind == HandleEntry::Kind::Static && !it->second.body_id.IsInvalid())
    {
        JPH::BodyInterface& bi = sPhysics->GetBodyInterface();
        bi.RemoveBody(it->second.body_id);
        bi.DestroyBody(it->second.body_id);
    }
    sHandles.erase(it);
}

BodyHandle addCharacter(const glm::vec3& position, float radius, float height)
{
    if (!sInit || radius <= 0.0f || height < 2.0f * radius)
        return kInvalidBody;
    const float cyl_half = height * 0.5f - radius;
    JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(cyl_half, radius);
    // Offset the capsule so its BOTTOM is at the controller's origin
    // (CharacterVirtual treats origin as the foot).
    JPH::Ref<JPH::Shape> shape = new JPH::RotatedTranslatedShape(
        JPH::Vec3(0, height * 0.5f, 0), JPH::Quat::sIdentity(), capsule);

    JPH::CharacterVirtualSettings settings;
    settings.mShape = shape;
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
    settings.mInnerBodyShape = shape;
    settings.mInnerBodyLayer = Layers::MOVING;
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3(0, 1, 0), -radius);
    // Internal-edge removal: when the swept capsule crosses a shared
    // triangle edge in a trimesh (walking over a hill curve), the
    // capsule normally snags on the edge and continues in its prior
    // direction instead of following the new triangle's slope.
    // Standard Jolt fix for trimesh-walking characters.
    settings.mEnhancedInternalEdgeRemoval = true;

    JPH::Ref<JPH::CharacterVirtual> ch = new JPH::CharacterVirtual(
        &settings, toJ(position), JPH::Quat::sIdentity(), 0, sPhysics.get());

    const BodyHandle h = allocHandle();
    HandleEntry entry;
    entry.kind = HandleEntry::Kind::Character;
    entry.character = ch;
    entry.tag = SurfaceTag::Actor;
    sHandles.emplace(h.id, std::move(entry));
    return h;
}

void setCharacterVelocity(BodyHandle character, const glm::vec3& v, bool apply_y)
{
    auto it = sHandles.find(character.id);
    if (it == sHandles.end() || !it->second.character) return;
    JPH::Vec3 current = it->second.character->GetLinearVelocity();
    JPH::Vec3 next(v.x, apply_y ? v.y : current.GetY(), v.z);
    it->second.character->SetLinearVelocity(next);
}

glm::vec3 characterPosition(BodyHandle character)
{
    auto it = sHandles.find(character.id);
    if (it == sHandles.end() || !it->second.character) return {0, 0, 0};
    return fromJ(it->second.character->GetPosition());
}

bool isCharacterOnGround(BodyHandle character)
{
    auto it = sHandles.find(character.id);
    if (it == sHandles.end() || !it->second.character) return false;
    using GS = JPH::CharacterVirtual::EGroundState;
    const GS s = it->second.character->GetGroundState();
    return s == GS::OnGround || s == GS::OnSteepGround;
}

BodyHandle characterGroundBody(BodyHandle character)
{
    auto it = sHandles.find(character.id);
    if (it == sHandles.end() || !it->second.character) return kInvalidBody;
    const JPH::BodyID ground_id = it->second.character->GetGroundBodyID();
    if (ground_id.IsInvalid()) return kInvalidBody;
    for (const auto& [hid, e] : sHandles)
        if (e.kind == HandleEntry::Kind::Static && e.body_id == ground_id)
            return BodyHandle{hid};
    return kInvalidBody;
}

void characterActiveContacts(BodyHandle character, std::vector<BodyHandle>& out)
{
    out.clear();
    auto it = sHandles.find(character.id);
    if (it == sHandles.end() || !it->second.character) return;
    for (const auto& c : it->second.character->GetActiveContacts())
    {
        for (const auto& [hid, e] : sHandles)
        {
            if (e.kind == HandleEntry::Kind::Static && e.body_id == c.mBodyB)
            {
                out.push_back(BodyHandle{hid});
                break;
            }
        }
    }
}

void characterActiveContactDetails(BodyHandle character,
                                   std::vector<CharacterContactDetail>& out)
{
    out.clear();
    auto it = sHandles.find(character.id);
    if (it == sHandles.end() || !it->second.character) return;
    for (const auto& c : it->second.character->GetActiveContacts())
    {
        CharacterContactDetail d;
        d.body = kInvalidBody;
        for (const auto& [hid, e] : sHandles)
        {
            if (e.kind == HandleEntry::Kind::Static && e.body_id == c.mBodyB)
            {
                d.body = BodyHandle{hid};
                break;
            }
        }
        d.position = glm::vec3(c.mPosition.GetX(), c.mPosition.GetY(), c.mPosition.GetZ());
        d.normal   = glm::vec3(c.mSurfaceNormal.GetX(),
                                c.mSurfaceNormal.GetY(),
                                c.mSurfaceNormal.GetZ());
        d.tri_v0 = d.tri_v1 = d.tri_v2 = glm::vec3(0.0f);
        d.is_trimesh_triangle = false;
        out.push_back(d);
    }
}

void teleportCharacter(BodyHandle character, const glm::vec3& position)
{
    auto it = sHandles.find(character.id);
    if (it == sHandles.end() || !it->second.character) return;
    it->second.character->SetPosition(toJ(position));
    it->second.character->SetLinearVelocity(JPH::Vec3::sZero());
}

RayHit raycast(const glm::vec3& origin, const glm::vec3& direction,
               float max_distance, BodyHandle ignore)
{
    RayHit hit;
    if (!sInit) return hit;

    glm::vec3 dir = direction;
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-6f) return hit;
    dir /= len;

    JPH::RRayCast ray(toJ(origin), toJ(dir * max_distance));
    JPH::RayCastResult result;
    // Static-only: rays are camera/world queries; ignore character
    // capsules (would self-hit the player otherwise).
    JPH::SpecifiedBroadPhaseLayerFilter bp_filter(BroadPhaseLayers::NON_MOVING);
    JPH::SpecifiedObjectLayerFilter     obj_filter(Layers::NON_MOVING);
    bool any = sPhysics->GetNarrowPhaseQuery().CastRay(
        ray, result, bp_filter, obj_filter);
    if (!any) return hit;

    // Filter out ignored body.
    (void)ignore; // TODO: BodyFilter for ignore; rare in our usage so far

    hit.hit = true;
    hit.distance = result.mFraction * max_distance;
    hit.position = origin + dir * hit.distance;
    // Look up body for normal + tag.
    JPH::BodyLockRead lock(sPhysics->GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded())
    {
        const JPH::Body& b = lock.GetBody();
        const JPH::Vec3 n = b.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, ray.GetPointOnRay(result.mFraction));
        hit.normal = fromJ(n);
    }
    // Find our handle for tag lookup.
    for (const auto& kv : sHandles)
    {
        if (kv.second.kind == HandleEntry::Kind::Static && kv.second.body_id == result.mBodyID)
        {
            hit.body = {kv.first};
            hit.tag = kv.second.tag;
            break;
        }
    }
    return hit;
}

bool sphereOverlap(const glm::vec3& center, float radius)
{
    if (!sInit || radius <= 0.0f) return false;

    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    JPH::CollideShapeSettings settings;
    settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideOnlyWithActive;
    settings.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;

    // Static-only filter: camera cares about world geometry, not the
    // player capsule or other character bodies.
    JPH::SpecifiedBroadPhaseLayerFilter bp_filter(BroadPhaseLayers::NON_MOVING);
    JPH::SpecifiedObjectLayerFilter     obj_filter(Layers::NON_MOVING);

    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
    sPhysics->GetNarrowPhaseQuery().CollideShape(
        &sphere,
        JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(toJ(center)),
        settings,
        JPH::Vec3::sZero(),
        collector,
        bp_filter,
        obj_filter);
    return collector.HadHit();
}

SurfaceTag bodySurfaceTag(BodyHandle body)
{
    auto it = sHandles.find(body.id);
    if (it == sHandles.end()) return SurfaceTag::Unknown;
    return it->second.tag;
}

void enumerateBodies(std::vector<BodyDebugInfo>& out)
{
    out.clear();
    if (!sInit) return;
    out.reserve(sHandles.size());
    const JPH::BodyLockInterfaceNoLock& lock_iface = sPhysics->GetBodyLockInterfaceNoLock();
    for (const auto& [id, entry] : sHandles)
    {
        BodyDebugInfo info;
        info.body = BodyHandle{id};
        info.tag  = entry.tag;
        if (entry.kind == HandleEntry::Kind::Character)
        {
            if (!entry.character) continue;
            info.kind = BodyKind::Character;
            const JPH::Vec3 pos = entry.character->GetPosition();
            // Pull capsule dims off the inner shape. Our character
            // wraps CapsuleShape in a RotatedTranslatedShape; the
            // bounding box from the controller includes everything
            // already, so AABB is straightforward.
            const JPH::AABox box = entry.character->GetShape()->GetWorldSpaceBounds(
                entry.character->GetWorldTransform(), JPH::Vec3::sReplicate(1.0f));
            info.world_aabb_min = fromJ(box.mMin);
            info.world_aabb_max = fromJ(box.mMax);
            info.capsule_center = fromJ(pos) + glm::vec3(0, (info.world_aabb_max.y - info.world_aabb_min.y) * 0.5f, 0);
            info.capsule_radius = (info.world_aabb_max.x - info.world_aabb_min.x) * 0.5f;
            info.capsule_half_height = (info.world_aabb_max.y - info.world_aabb_min.y) * 0.5f - info.capsule_radius;
            out.push_back(info);
            continue;
        }
        // Static body — figure kind from the shape sub-type, then
        // pull its world-space AABB.
        JPH::BodyLockRead lock(lock_iface, entry.body_id);
        if (!lock.Succeeded()) continue;
        const JPH::Body& b = lock.GetBody();
        const JPH::Shape* shape = b.GetShape();
        info.kind = BodyKind::StaticTrimesh;
        if (shape && shape->GetSubType() == JPH::EShapeSubType::Box)
            info.kind = BodyKind::StaticBox;
        const JPH::AABox box = b.GetWorldSpaceBounds();
        info.world_aabb_min = fromJ(box.mMin);
        info.world_aabb_max = fromJ(box.mMax);
        out.push_back(info);
    }
}

} // namespace engine::physics
