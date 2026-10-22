#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "nvse/PluginAPI.h"

namespace
{
constexpr std::uint32_t kPluginVersion = 6;
constexpr std::uintptr_t kPlayerAddress = 0x011DEA3C;
constexpr std::uintptr_t kFormsMapAddress = 0x011C54C0;
constexpr std::array<std::uintptr_t, 6> kHitCallSites = {
	0x0087C4DA, 0x0089A738, 0x008B91E1,
	0x009B0503, 0x009C1E96, 0x009CBDE8
};
constexpr std::array<std::uintptr_t, 6> kProjectileVtables = {
	0x010900DC, // Projectile
	0x0108C3C4, // BeamProjectile
	0x0108EA64, // ContinuousBeamProjectile
	0x0108F2F4, // FlameProjectile
	0x0108F674, // GrenadeProjectile
	0x0108FA44  // MissileProjectile
};
constexpr std::uintptr_t kHandleTracerVtableOffset = 0x30C;
constexpr std::uintptr_t kUpdateProjectileVtableOffset = 0x310;
constexpr std::uintptr_t kProjectileSourceRefOffset = 0x0FC;
constexpr std::uintptr_t kExplosionVtable = 0x0108EE04;
constexpr std::uintptr_t kExplosionSourceRefOffset = 0x0C8;
constexpr std::uintptr_t kFormIDOffset = 0x00C;
constexpr std::size_t kMaximumDeferredHits = 4096;

constexpr char kFreezeEvent[] = "TheWorldNV:FreezeHitQueue";
constexpr char kReleaseEvent[] = "TheWorldNV:ReleaseHitQueue";
constexpr char kDiscardEvent[] = "TheWorldNV:DiscardHitQueue";
constexpr char kBeforeHitsReplayedEvent[] = "TheWorldNV:BeforeHitsReplayed";
constexpr char kHitsReplayedEvent[] = "TheWorldNV:HitsReplayed";

class Actor;

struct NiPoint3
{
	float x;
	float y;
	float z;
};

// FalloutNV 1.4.0.525 ActorHitData. The whole structure is retained so the
// engine, rather than this plugin, remains responsible for every consequence.
struct ActorHitData
{
	Actor* source;                 // 00
	Actor* target;                 // 04
	void* projectile;              // 08
	std::uint32_t weaponAV;         // 0C
	std::int32_t hitLocation;       // 10
	float healthDmg;                // 14
	float wpnBaseDmg;               // 18
	float fatigueDmg;               // 1C
	float limbDmg;                  // 20
	float blockDTMod;               // 24
	float armorDmg;                 // 28
	float weaponDmg;                // 2C
	void* weapon;                   // 30
	float weapHealthPerc;           // 34
	NiPoint3 impactPos;             // 38
	NiPoint3 impactAngle;           // 44
	void* critHitEffect;            // 50
	void* ptr54;                    // 54
	std::uint32_t flags;            // 58
	float dmgMult;                  // 5C
	std::int32_t unk60;             // 60
};
static_assert(sizeof(ActorHitData) == 0x64, "Unexpected ActorHitData layout");

using HitMe = void(__thiscall*)(Actor*, ActorHitData*, char);
using HitHook = void(__fastcall*)(Actor*, void*, ActorHitData*, char);
using HandleTracer = void(__thiscall*)(void*);
using TracerHook = void(__fastcall*)(void*, void*);
using UpdateProjectile = void(__thiscall*)(void*, float);
using ProjectileUpdateHook = void(__fastcall*)(void*, void*, float);

// Runtime form table used by xNVSE's LookupFormByID implementation. The
// executable address 0x004F9620 is the GECK/editor lookup routine and must not
// be called by a FalloutNV.exe plugin.
struct FormMapEntry
{
	FormMapEntry* next;
	std::uint32_t key;
	void* data;
};

struct FormMap
{
	void* vtable;
	std::uint32_t bucketCount;
	FormMapEntry** buckets;
	std::uint32_t itemCount;
};

enum class HitCauseKind : std::uint8_t
{
	None,
	Projectile,
	Explosion
};

struct DeferredHit
{
	Actor* target;
	Actor* source;
	ActorHitData data;
	void* cause;
	std::uint32_t causeFormID;
	HitCauseKind causeKind;
	HitMe continuation;
	char attackClass;
};

PluginHandle s_pluginHandle = static_cast<PluginHandle>(kPluginHandle_Invalid);
NVSEMessagingInterface* s_messaging = nullptr;
NVSEEventManagerInterface* s_events = nullptr;

std::array<HitMe, kHitCallSites.size()> s_continuations{};
std::array<HandleTracer, kProjectileVtables.size()> s_tracerContinuations{};
std::array<UpdateProjectile, kProjectileVtables.size()> s_projectileUpdateContinuations{};
std::array<DeferredHit, kMaximumDeferredHits> s_pendingHits{};
std::array<DeferredHit, kMaximumDeferredHits> s_replayHits{};
std::size_t s_pendingHitCount = 0;
CRITICAL_SECTION s_queueLock{};

volatile LONG s_frozen = FALSE;
volatile LONG s_releasePending = FALSE;
volatile LONG s_replaying = FALSE;
volatile LONG s_queueOverflowReported = FALSE;
bool s_hooksInstalled = false;
bool s_tracerHooksInstalled = false;

Actor* GetPlayer()
{
	return *reinterpret_cast<Actor**>(kPlayerAddress);
}

HitCauseKind GetHitCauseKind(void* cause)
{
	if (!cause)
		return HitCauseKind::None;

	const auto vtable = *reinterpret_cast<std::uintptr_t*>(cause);
	for (const auto projectileVtable : kProjectileVtables)
	{
		if (vtable == projectileVtable)
			return HitCauseKind::Projectile;
	}
	return vtable == kExplosionVtable ? HitCauseKind::Explosion : HitCauseKind::None;
}

std::uint32_t GetFormID(void* form)
{
	return form ? *reinterpret_cast<std::uint32_t*>(
		reinterpret_cast<std::uint8_t*>(form) + kFormIDOffset) : 0;
}

void* ResolveHitCause(const DeferredHit& hit)
{
	if (!hit.cause || !hit.causeFormID || hit.causeKind == HitCauseKind::None)
		return nullptr;

	auto* formsMap = *reinterpret_cast<FormMap**>(kFormsMapAddress);
	if (!formsMap || !formsMap->buckets || !formsMap->bucketCount)
		return nullptr;

	void* resolved = nullptr;
	for (auto* entry = formsMap->buckets[hit.causeFormID % formsMap->bucketCount];
		entry; entry = entry->next)
	{
		if (entry->key == hit.causeFormID)
		{
			resolved = entry->data;
			break;
		}
	}
	// Dynamic reference IDs may be recycled. Pointer and class must both still
	// match before the delayed transaction is allowed to use the cause object.
	if (resolved != hit.cause || GetHitCauseKind(resolved) != hit.causeKind)
		return nullptr;
	return resolved;
}

void RestoreHitOwnership(DeferredHit& hit)
{
	// source is the authoritative attacker used by damage, death, XP, crime, and
	// hostility. Keep it outside ActorHitData so no secondary object can replace it.
	hit.data.source = hit.source;
	hit.data.target = hit.target;

	void* cause = ResolveHitCause(hit);
	hit.data.projectile = cause;
	if (!cause)
		return;

	const std::uintptr_t sourceOffset = hit.causeKind == HitCauseKind::Projectile
		? kProjectileSourceRefOffset
		: kExplosionSourceRefOffset;
	*reinterpret_cast<Actor**>(
		reinterpret_cast<std::uint8_t*>(cause) + sourceOffset) = hit.source;
}

void DebugMessage(const char* message)
{
	OutputDebugStringA(message);
}

bool WriteRelativeCall(std::uintptr_t callSite, const void* destination, HitMe* original = nullptr)
{
	auto* instruction = reinterpret_cast<std::uint8_t*>(callSite);
	if (instruction[0] != 0xE8)
		return false;

	if (original)
	{
		const auto displacement = *reinterpret_cast<std::int32_t*>(instruction + 1);
		*original = reinterpret_cast<HitMe>(callSite + 5 + displacement);
	}

	const auto relative = reinterpret_cast<std::uintptr_t>(destination) - (callSite + 5);
	DWORD oldProtection = 0;
	if (!VirtualProtect(instruction, 5, PAGE_EXECUTE_READWRITE, &oldProtection))
		return false;

	instruction[0] = 0xE8;
	*reinterpret_cast<std::int32_t*>(instruction + 1) = static_cast<std::int32_t>(relative);
	FlushInstructionCache(GetCurrentProcess(), instruction, 5);

	DWORD ignored = 0;
	VirtualProtect(instruction, 5, oldProtection, &ignored);
	return true;
}

bool WritePointer(std::uintptr_t address, const void* value)
{
	DWORD oldProtection = 0;
	auto* destination = reinterpret_cast<void**>(address);
	if (!VirtualProtect(destination, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtection))
		return false;

	*destination = const_cast<void*>(value);
	FlushInstructionCache(GetCurrentProcess(), destination, sizeof(void*));

	DWORD ignored = 0;
	VirtualProtect(destination, sizeof(void*), oldProtection, &ignored);
	return true;
}

void ClearQueue()
{
	EnterCriticalSection(&s_queueLock);
	s_pendingHitCount = 0;
	LeaveCriticalSection(&s_queueLock);
}

bool QueueHit(Actor* target, const ActorHitData* hitData, char attackClass, HitMe continuation)
{
	EnterCriticalSection(&s_queueLock);
	if (s_pendingHitCount >= s_pendingHits.size())
	{
		LeaveCriticalSection(&s_queueLock);
		if (InterlockedCompareExchange(&s_queueOverflowReported, TRUE, FALSE) == FALSE)
			DebugMessage("TheWorldNV: deferred-hit queue full; newest hit was suppressed.\n");
		return false;
	}

	auto& queued = s_pendingHits[s_pendingHitCount++];
	queued.target = target;
	queued.source = hitData->source;
	queued.data = *hitData;
	queued.cause = hitData->projectile;
	queued.causeKind = GetHitCauseKind(queued.cause);
	queued.causeFormID = queued.causeKind != HitCauseKind::None
		? GetFormID(queued.cause)
		: 0;
	queued.continuation = continuation;
	queued.attackClass = attackClass;
	LeaveCriticalSection(&s_queueLock);
	return true;
}

template <std::size_t Index>
void __fastcall HitMeHook(Actor* target, void*, ActorHitData* hitData, char attackClass)
{
	const HitMe continuation = s_continuations[Index];
	if (!continuation)
		return;

	if (InterlockedCompareExchange(&s_replaying, FALSE, FALSE) == FALSE &&
		InterlockedCompareExchange(&s_frozen, FALSE, FALSE) != FALSE &&
		hitData && hitData->source == GetPlayer())
	{
		// Whether queued or dropped at the hard safety limit, do not let the
		// original transaction expose any hit consequence during stopped time.
		QueueHit(target, hitData, attackClass, continuation);
		return;
	}

	continuation(target, hitData, attackClass);
}

template <std::size_t Index>
void __fastcall HandleTracerHook(void* projectile, void*)
{
	const HandleTracer continuation = s_tracerContinuations[Index];
	if (!continuation)
		return;

	if (InterlockedCompareExchange(&s_frozen, FALSE, FALSE) != FALSE && projectile)
	{
		Actor* source = *reinterpret_cast<Actor**>(
			reinterpret_cast<std::uint8_t*>(projectile) + kProjectileSourceRefOffset);
		if (source == GetPlayer())
			return;
	}

	continuation(projectile);
}

bool IsFrozenPlayerProjectile(void* projectile)
{
	if (InterlockedCompareExchange(&s_frozen, FALSE, FALSE) == FALSE || !projectile)
		return false;

	Actor* source = *reinterpret_cast<Actor**>(
		reinterpret_cast<std::uint8_t*>(projectile) + kProjectileSourceRefOffset);
	return source == GetPlayer();
}

template <std::size_t Index>
void __fastcall UpdateProjectileHook(void* projectile, void*, float timePassed)
{
	const UpdateProjectile continuation = s_projectileUpdateContinuations[Index];
	if (!continuation)
		return;

	// Standard hitscan tracers are advanced from UpdateProjectile rather than
	// the base Projectile::HandleTracer slot. Suppressing both paths prevents a
	// newly fired trail from being created or extended until time resumes.
	if (IsFrozenPlayerProjectile(projectile))
		return;

	continuation(projectile, timePassed);
}

bool InstallHitHooks()
{
	if (s_hooksInstalled)
		return true;

	static constexpr std::array<HitHook, kHitCallSites.size()> hooks = {
		HitMeHook<0>, HitMeHook<1>, HitMeHook<2>,
		HitMeHook<3>, HitMeHook<4>, HitMeHook<5>
	};

	std::size_t installed = 0;
	for (; installed < kHitCallSites.size(); ++installed)
	{
		if (WriteRelativeCall(kHitCallSites[installed],
			reinterpret_cast<const void*>(hooks[installed]), &s_continuations[installed]))
			continue;

		for (std::size_t rollback = 0; rollback < installed; ++rollback)
			WriteRelativeCall(kHitCallSites[rollback],
				reinterpret_cast<const void*>(s_continuations[rollback]));

		DebugMessage("TheWorldNV: Actor::HitMe interception could not be installed.\n");
		return false;
	}

	s_hooksInstalled = true;
	DebugMessage("TheWorldNV: Actor::HitMe interception installed.\n");
	return true;
}

bool InstallTracerHooks()
{
	if (s_tracerHooksInstalled)
		return true;

	static constexpr std::array<TracerHook, kProjectileVtables.size()> hooks = {
		HandleTracerHook<0>, HandleTracerHook<1>, HandleTracerHook<2>,
		HandleTracerHook<3>, HandleTracerHook<4>, HandleTracerHook<5>
	};
	static constexpr std::array<ProjectileUpdateHook, kProjectileVtables.size()> updateHooks = {
		UpdateProjectileHook<0>, UpdateProjectileHook<1>, UpdateProjectileHook<2>,
		UpdateProjectileHook<3>, UpdateProjectileHook<4>, UpdateProjectileHook<5>
	};

	std::size_t installed = 0;
	for (; installed < kProjectileVtables.size(); ++installed)
	{
		const std::uintptr_t tracerSlot = kProjectileVtables[installed] + kHandleTracerVtableOffset;
		const std::uintptr_t updateSlot = kProjectileVtables[installed] + kUpdateProjectileVtableOffset;
		auto originalTracer = *reinterpret_cast<HandleTracer*>(tracerSlot);
		auto originalUpdate = *reinterpret_cast<UpdateProjectile*>(updateSlot);
		if (!originalTracer || !originalUpdate ||
			!WritePointer(tracerSlot, reinterpret_cast<const void*>(hooks[installed])))
		{
			for (std::size_t rollback = 0; rollback < installed; ++rollback)
			{
				WritePointer(kProjectileVtables[rollback] + kHandleTracerVtableOffset,
					reinterpret_cast<const void*>(s_tracerContinuations[rollback]));
				WritePointer(kProjectileVtables[rollback] + kUpdateProjectileVtableOffset,
					reinterpret_cast<const void*>(s_projectileUpdateContinuations[rollback]));
			}
			DebugMessage("TheWorldNV: projectile tracer interception could not be installed.\n");
			return false;
		}

		s_tracerContinuations[installed] = originalTracer;
		s_projectileUpdateContinuations[installed] = originalUpdate;
		if (!WritePointer(updateSlot, reinterpret_cast<const void*>(updateHooks[installed])))
		{
			WritePointer(tracerSlot, reinterpret_cast<const void*>(originalTracer));
			for (std::size_t rollback = 0; rollback < installed; ++rollback)
			{
				WritePointer(kProjectileVtables[rollback] + kHandleTracerVtableOffset,
					reinterpret_cast<const void*>(s_tracerContinuations[rollback]));
				WritePointer(kProjectileVtables[rollback] + kUpdateProjectileVtableOffset,
					reinterpret_cast<const void*>(s_projectileUpdateContinuations[rollback]));
			}
			DebugMessage("TheWorldNV: projectile update interception could not be installed.\n");
			return false;
		}
	}

	s_tracerHooksInstalled = true;
	DebugMessage("TheWorldNV: projectile tracer/update interception installed.\n");
	return true;
}

void ReplayQueuedHits()
{
	if (InterlockedCompareExchange(&s_frozen, FALSE, FALSE) != FALSE ||
		InterlockedExchange(&s_releasePending, FALSE) == FALSE)
		return;

	EnterCriticalSection(&s_queueLock);
	const std::size_t replayCount = s_pendingHitCount;
	if (replayCount)
		std::memcpy(s_replayHits.data(), s_pendingHits.data(), replayCount * sizeof(DeferredHit));
	s_pendingHitCount = 0;
	LeaveCriticalSection(&s_queueLock);

	InterlockedExchange(&s_replaying, TRUE);
	// Both events execute synchronously on this main-loop callback. Scripts make
	// Player detectable only across the continuations below, so normal AI never
	// receives a frame in which it can learn Player's frozen-time position.
	if (s_events)
		s_events->DispatchEvent(kBeforeHitsReplayedEvent, nullptr);
	for (std::size_t index = 0; index < replayCount; ++index)
	{
		auto& hit = s_replayHits[index];
		if (hit.target && hit.continuation)
		{
			RestoreHitOwnership(hit);
			hit.continuation(hit.target, &hit.data, hit.attackClass);
		}
	}
	InterlockedExchange(&s_replaying, FALSE);

	// Script-side perception is temporarily restored before release. Notify it
	// only after every hit has retained its real Player source and combat/crime
	// reactions have had a chance to select that source as their target.
	if (s_events)
		s_events->DispatchEvent(kHitsReplayedEvent, nullptr);
}

void OnFreeze(TESObjectREFR*, void*)
{
	InterlockedExchange(&s_releasePending, FALSE);
	InterlockedExchange(&s_frozen, TRUE);
}

void OnRelease(TESObjectREFR*, void*)
{
	InterlockedExchange(&s_frozen, FALSE);
	InterlockedExchange(&s_releasePending, TRUE);
}

void OnDiscard(TESObjectREFR*, void*)
{
	InterlockedExchange(&s_frozen, FALSE);
	InterlockedExchange(&s_releasePending, FALSE);
	ClearQueue();
}

void MessageHandler(NVSEMessagingInterface::Message* message)
{
	switch (message->type)
	{
	case NVSEMessagingInterface::kMessage_MainGameLoop:
		ReplayQueuedHits();
		break;
	case NVSEMessagingInterface::kMessage_PreLoadGame:
	case NVSEMessagingInterface::kMessage_ExitToMainMenu:
	case NVSEMessagingInterface::kMessage_NewGame:
	case NVSEMessagingInterface::kMessage_ExitGame:
		OnDiscard(nullptr, nullptr);
		break;
	default:
		break;
	}
}
}

extern "C"
{
__declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info)
{
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "TheWorldNV";
	info->version = kPluginVersion;

	if (nvse->isEditor || nvse->isNogore)
		return false;
	if (nvse->runtimeVersion != RUNTIME_VERSION_1_4_0_525)
		return false;
	return nvse->nvseVersion >= MAKE_NEW_VEGAS_VERSION(6, 3, 5);
}

__declspec(dllexport) bool NVSEPlugin_Load(NVSEInterface* nvse)
{
	s_pluginHandle = nvse->GetPluginHandle();
	InitializeCriticalSection(&s_queueLock);

	s_messaging = static_cast<NVSEMessagingInterface*>(nvse->QueryInterface(kInterface_Messaging));
	s_events = static_cast<NVSEEventManagerInterface*>(nvse->QueryInterface(kInterface_EventManager));
	if (!s_messaging || !s_events)
		return false;

	const auto flags = NVSEEventManagerInterface::kFlag_AllowScriptDispatch;

	s_events->RegisterEvent(kFreezeEvent, 0, nullptr, flags);
	s_events->RegisterEvent(kReleaseEvent, 0, nullptr, flags);
	s_events->RegisterEvent(kDiscardEvent, 0, nullptr, flags);
	s_events->RegisterEvent(kBeforeHitsReplayedEvent, 0, nullptr, flags);
	s_events->RegisterEvent(kHitsReplayedEvent, 0, nullptr, flags);

	if (!s_events->SetNativeEventHandler(kFreezeEvent, OnFreeze) ||
		!s_events->SetNativeEventHandler(kReleaseEvent, OnRelease) ||
		!s_events->SetNativeEventHandler(kDiscardEvent, OnDiscard))
		return false;

	if (!s_messaging->RegisterListener(s_pluginHandle, "NVSE", MessageHandler))
		return false;

	// Install before reporting a successful plugin load. This makes the scripts'
	// GetPluginVersion guard a real fail-safe if any call site is unavailable.
	if (!InstallHitHooks())
		return false;

	// Tracer suppression is visual. If an incompatible plugin owns an unusual
	// vtable slot, keep the proven hit deferral loaded instead of unloading code
	// that already has live Actor::HitMe call hooks.
	InstallTracerHooks();
	return true;
}
}
