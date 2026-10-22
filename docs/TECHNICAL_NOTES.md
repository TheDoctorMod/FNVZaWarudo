# Technical notes

## Chosen mechanism

JIP LN NVSE's `FreezeTime` stops game-time, actor animation, Havok physics, and
physical projectile simulation while exempting the player. It does not defer
New Vegas hitscan collision: a frozen tracer can coexist with an already
processed hit. `OnHit` and JIP's `SetOnHitEventHandler` run before damage but do
not provide cancellation, so restoring health afterward is not safe.

The script-only projectile substitution did not solve this: an equipped weapon
can retain cached hitscan data even after its ammo projectile is rerouted. The
replacement is a small xNVSE DLL that intercepts all six direct call sites for
FalloutNV 1.4.0.525 `Actor::HitMe`. While the exact freeze is owned by this mod,
it copies the complete 0x64-byte `ActorHitData` transaction for player-sourced
hits and returns before the original function can run.

On normal resume, the DLL disarms interception before JIP unfreezes the world
and replays each stored transaction through the exact continuation captured at
its original call site on the following main-loop tick. Consequently the engine
still owns DT/DR, critical, limb, fatigue, armor and weapon damage, hit events,
death, XP, and quest behavior, but none of those results exist while frozen.
The per-call-site continuation also preserves another well-behaved call hook in
the chain regardless of relative plugin load order.

Hitscan tracers remain cosmetic: their collision was already chosen at fire
time, so queued consequences are released just after time resumes rather than
being synchronized to the later tracer endpoint. Physical projectile motion is
still frozen and resumed by JIP itself. Because missed hitscan shots never enter
`Actor::HitMe`, the native plugin separately chains the `HandleTracer` and
`UpdateProjectile` vtable slots of all six New Vegas projectile classes. It
skips player-owned tracer and projectile updates while the freeze flag is set,
but does not suppress
`Do3DLoaded`. Consequently a projectile form that supplies visible 3D remains
at its initial firing transform and keeps its original aim, while no moving
trail is generated. A purely hitscan form with no visible projectile model
remains invisible; the plugin does not substitute records or fabricate a NIF.

Normal resume leaves the detection override active until the native replay
callback. The DLL synchronously dispatches `TheWorldNV:BeforeHitsReplayed`,
runs all queued `ActorHitData` continuations, then dispatches
`TheWorldNV:HitsReplayed` before returning to the engine. The first event makes
Player detectable; the second rebuilds the forced-target set after crime/combat
reactions have seen `ActorHitData.source == Player`, then immediately reapplies
the detection override. No AI frame can learn Player's frozen-time position
between those events. This order prevents newly hostile witnesses from choosing
an unrelated detectable actor when Player becomes hidden. The original
projectile class implementations resume afterward.

## Quantum Trigger reference

Quantum Trigger documents the same observable technique: all projectiles freeze
in place when fired and continue when time is unfrozen. It also documents the
same cell-streaming and `TFC 1` cautions. No Quantum Trigger code or assets are
included or copied here.

## Lifecycle strategy

- A `gr_` JIP runner script compiles and registers the UDFs once per process.
- Temporary public JIP auxiliary variables on `Player` hold controller state;
  they do not bake persistent mod state into a save.
- The controller has READY, ACTIVE, and COOLDOWN states. Only ACTIVE owns the
  exact JIP freeze. It never reads or writes the global time multiplier, so the
  player remains at precisely the same simulation speed. Audio and visual cues
  do not control simulation timing.
- `gr_TheWorldNV.txt` owns the centralized 10.0-second duration, 12.0 AP/second
  drain, and 20.0-second cooldown constants. Temporary `*` auxiliary variables
  keep these runtime states out of saves while preserving them across loads in
  the same game process.
- ACTIVE accumulates `GetTimePlayed 1` deltas only during gameplay. Menu frames
  reset the sample and do not count toward duration or AP cost. COOLDOWN uses an
  absolute session-time deadline and may expire while a menu is open.
- APRemaining is an authoritative current-AP ceiling. Each active update first
  honors any external AP expenditure, subtracts exactly `12 * realDelta`, then
  applies only the excess through vanilla `DamageAV ActionPoints`. Engine AP
  regeneration cannot raise current AP above the ceiling, and stopping the
  enforcement immediately restores the player's normal regeneration rules.
- The per-frame callback runs in game and menu modes, but input is deliberately
  ignored in menus.
- Load-game cleanup is deferred four frames rather than executed in the load
  event, following JIP's explicit crash warning.
- The OnActivate handler restores time before a load door performs its normal
  activation. Ordinary exterior cell-border crossings do not alter the freeze
  or the queued-hit buffer.
- The controller refuses to take ownership when time is already frozen, and it
  removes only the IMOD/state it owns if another system restores time.
- Deferred storage is a fixed 4096-entry buffer allocated at plugin load. At
  the hard limit, additional hits are suppressed rather than leaked into the
  frozen world. No allocation occurs in the hit hook.
- Load/new game, main-menu exit, death, load-door cancellation, and external
  unfreeze discard unsafe queued pointers. Exterior cell borders preserve the
  queue because adjacent world cells remain streamed. Only an ordinary H-key
  resume releases queued hits. The DLL also clears the queue directly from NVSE
  lifecycle messages, independently of the scripts.
- Three private script-dispatchable NVSE events control freeze, release, and
  discard state. No plugin opcode range is consumed.

## Perception

`FreezeTime` suspends AI execution but does not promise that the detection
service will stop updating its target data. During the exact freeze the player
is forced undetectable with JIP `ForceActorDetectionValue`. Hostility, factions,
combat membership, crime state, and packages are not reset. Enemies therefore
retain their pre-freeze combat state without receiving valid detection updates
for the player's frozen-time path. On normal H-key resume, the forced value
remains for `fPostResumeHiddenSeconds` real seconds, giving AI time to search
its last known position. Actors whose combat target was already the player are
temporarily held on that target with JIP's targeted `SetCombatDisabled` mode, so
the concealment cannot make them select unrelated actors. Only locks created by
this mod are released afterward. This does not force crouching, modify factions,
or change movement speed.

JIP exposes no getter for a pre-existing forced detection override. Clearing
the override on resume can therefore conflict with another mod applying that
same low-level override to the player. `Perception:bSuppressPlayerDetection`
can be disabled for such setups.

## Audio

The installed JohnnyGuitar build reports version 5.15. Its `PlaySoundFile`
command routes through the game's special music/media streamer rather than the
normal sound-effect pipeline, so it is not used for these cues. On JohnnyGuitar
5.16 and older, runtime clones of `UIVATSEnter` and `UIVATSExit` are redirected
to the custom PCM WAV files with JIP's `SetSoundSourceFile` and played as system
sounds. JohnnyGuitar 5.17 and newer instead use `PlaySoundFromPath` through a
runtime snippet. The original VATS Sound forms remain the final fallback.
Path string variables passed from JIP Script Runner UDFs are wrapped as inline
expressions; passing the bare variable name makes the command receive that name
instead of the configured path.

JIP `SetMusicState` provides a real pause/resume cursor for background music.
The public APIs expose only category volume for dialogue voice and radio, so the
controller snapshots and mutes those buses, then restores their exact values.
This avoids terminating dialogue or changing user INIs, but voice/radio streams
can advance silently. Pausing those stream cursors and selectively pausing
effects emitted by frozen entities remains a native-audio-hook task.

## Primary references

- https://geckwiki.com/index.php?title=FreezeTime
- https://geckwiki.com/index.php?title=JIP_Script_Runner
- https://geckwiki.com/index.php?title=SetGameMainLoopCallback
- https://geckwiki.com/index.php?title=NVSE_Event_Handling
- https://geckwiki.com/index.php?title=SetImageSpaceModTrait
- https://geckwiki.com/index.php?title=Category:Auxiliary-Variable_Functions_(JIP)
- https://geckwiki.com/index.php?title=GameGetSecondsPassed
- https://geckwiki.com/index.php?title=EditorIDToFormID
- https://geckwiki.com/index.php?title=Projectile
- https://geckwiki.com/index.php?title=SetProjectileFlag
- https://geckwiki.com/index.php?title=SetProjectileTraitNumeric
- https://github.com/xNVSE/NVSE
- https://geckwiki.com/index.php?title=ForceActorDetectionValue
- https://geckwiki.com/index.php?title=SetMusicState
- https://geckwiki.com/index.php?title=SetGameVolume
- https://geckwiki.com/index.php?title=DisablePlayerControlsAltEx
- https://geckwiki.com/index.php?title=NVSE_Event_Handling
- https://geckwiki.com/index.php?title=ITR%3AOnPreHitDamage
- https://geckwiki.com/index.php?title=PlaySoundFromPath
- https://geckwiki.com/index.php?title=PlaySoundFile
- https://github.com/jazzisparis/JIP-LN-NVSE
- https://github.com/carxt/JohnnyGuitarNVSE
- https://github.com/xNVSE/NVSE
- https://github.com/Into-The-Rough/itr-nvse

Quantum Trigger was used only as a behavioral and limitations reference:
https://www.nexusmods.com/newvegas/mods/63172
# Activation shockwave

UIO injects `menus\prefabs\TheWorldNV\ActivationRing.xml` into `HUDMainMenu`.
The controller writes a smoothstep progress value to
`HUDMainMenu\TheWorldNV\_TWNVProgress` using real session time. Phase 1 already
owns `FreezeTime` and the native hit queue; only application of the persistent
IMOD waits for the ring to reach the edge. Phase 2 is the normal active frozen
state. All stop and lifecycle paths reset the HUD progress to zero.

