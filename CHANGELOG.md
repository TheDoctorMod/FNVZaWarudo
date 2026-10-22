# Changelog

## 0.4.0 - 2026-10-20

- Added explicit READY, ACTIVE, and COOLDOWN session states.
- Limited each activation to 10 seconds of unpaused real gameplay time.
- Added continuous delta-time AP consumption at exactly 12 AP per second, with
  immediate resume at zero and no negative AP.
- Neutralized natural AP regeneration during ACTIVE through an authoritative
  current-AP ceiling and vanilla `DamageAV`, without changing global settings,
  base actor values, maximum AP, perks, or equipment modifiers.
- Added a 20-second real-time cooldown after every manual, automatic, death,
  load-door, load, or external-unfreeze cleanup path.
- Made menu frames pause active duration and AP cost while allowing cooldown
  and normal post-resume AP regeneration to continue.
- Centralized all three balance constants in the JIP session bootstrap.

## 0.3.1 - 2026-10-20

- Replaced `Player.GetCombatTargets` target capture with a scan of all active
  high-process actors using `IsInCombatWith Player`.
- Newly hostile witnesses are now captured even if their current selected
  target has already drifted to a nearby NPC before concealment is reapplied.
- Kept detection restoration confined to the synchronous native replay bracket;
  no exposed AI frame is introduced by this correction.

## 0.3.0 - 2026-10-20

- Added synchronous `BeforeHitsReplayed`/`HitsReplayed` brackets around the
  native hit continuations.
- Kept the player concealed through unfreeze, restored detection only while
  damage/crime systems attributed queued attacks, captured newly hostile
  witnesses, and reapplied concealment before returning control to the engine.
- Removed the previous one-main-loop detection window that could disclose the
  player's new position before queued hits were replayed.
- Raised the required native plugin version to 4.

## 0.2.9 - 2026-10-20

- Added the native `TheWorldNV:HitsReplayed` completion event and moved
  post-resume concealment behind it.
- Restored normal detection only while queued attacks are attributed, then
  rebuilt player-target locks to include witnesses made hostile by a kill.
  Those witnesses can no longer redirect aggression toward an innocent NPC
  merely because the actual attacker has been made artificially undetectable.
- Kept projectile 3D creation separate from suspended tracer/update handlers,
  allowing projectile forms with visible models to remain at their firing
  origin and face their original trajectory without producing a moving trail.
- Raised the required native plugin version to 3.

## 0.2.8 - 2026-10-20

- Added interception of `Projectile::UpdateProjectile` for all six projectile
  classes because ordinary hitscan bullet trails do not use the base class's
  nearly empty `HandleTracer` implementation.
- Player-owned projectile/trail updates now remain completely dormant while
  time is stopped and return to the original engine handlers only after resume.
- Retained the specialized `HandleTracer` hooks for beam, continuous-beam, and
  flame projectile behavior.
- Raised the native plugin version guard to 2 so the older incomplete tracer
  interception cannot be used silently with the updated scripts.

## 0.2.7 - 2026-10-20

- Added a UIO-injected gold circular shockwave that expands from the centre of
  the screen before the static inverted palette appears.
- Kept the world freeze immediate while the visual introduction advances from
  real session time, preserving the ability's gameplay response.
- Added live INI controls for enabling the ring, its 0.48-second duration, and
  its HUD opacity.
- Made interruption and lifecycle cleanup hide and reset a partial wave.

## 0.2.6 - 2026-10-20

- Added native `Projectile::HandleTracer` interception for all six New Vegas
  projectile classes. Player-owned trails no longer continue updating while
  time is stopped, including missed shots that never create `ActorHitData`.
- Preserved each class's previous vtable continuation so other well-behaved
  projectile hooks remain chained.

## 0.2.5 - 2026-10-20

- Locked NPCs already focused on the player to that same combat target during
  frozen/post-resume concealment, preventing target loss from redirecting their
  aggression toward unrelated actors.
- Tracked and released only combat-target locks owned by The World NV; factions,
  aggression, confidence, and pre-existing third-party locks remain untouched.

## 0.2.4 - 2026-10-20

- Preserved accumulated hit transactions across ordinary exterior cell borders.
  The previous safety cleanup could silently discard an entire frozen-time
  burst when the player crossed an invisible world-cell boundary.

## 0.2.3 - 2026-10-20

- Stopped treating ordinary exterior cell-border crossings as a request to end
  the ability. The queued-hit buffer is now cleared and re-armed while JIP's
  freeze remains active.
- Added a configurable 1.75-second post-resume concealment window so NPCs search
  their last known position instead of immediately tracking the player's new
  location.
- Kept load doors, save loading, death, menu exit, and externally cleared time
  as mandatory lifecycle cleanup boundaries.

## 0.2.2 - 2026-10-20

- Removed activation/resume manipulation of the global time multiplier. The
  player now retains exactly the same movement and handling speed across any
  number of time-stop activations.
- Replaced the three-phase slow-motion controller with an immediate binary
  `FreezeTime` toggle while retaining independent sounds and visual effects.
- Removed the obsolete `[TimeTransition]` configuration section.

## 0.2.1 - 2026-10-20

- Replaced projectile-form substitution after testing showed that the equipped
  weapon could retain cached hitscan collision and apply health/limb damage
  while its tracer remained frozen.
- Added source for a Win32 xNVSE interceptor covering all six
  `Actor::HitMe` call sites. Complete player hit transactions now remain queued
  until normal resume, preventing premature damage, cripple, death, XP, events,
  and notifications.
- Added a fixed 4096-hit safety buffer, call-chain preservation, next-main-loop
  replay, and independent discard handling for loads, new games, menu exit,
  death, cell transitions, load doors, and external unfreeze.
- Made the native DLL mandatory at activation so a missing interceptor fails
  visibly and safely.

## 0.2.0 - 2026-10-20

- Removed the duration, expiration, and manual-cancel configuration paths; the
  hotkey is now an unlimited ON/OFF toggle.
- Corrected the false assumption that visually frozen hitscan tracers imply
  deferred collision. Effective ammo/weapon projectiles now use cached physical
  runtime clones during the exact freeze and restore before time resumes.
- Added cleanup-safe projectile routing for weapon switching, load, cell/death
  cancellation, and external unfreeze without replaying scripted damage.
- Suppressed player detection updates during the exact freeze without clearing
  hostility, factions, crime state, or combat membership.
- Added real background-music pause/resume and reversible voice/radio category
  muting, with exact prior volume restoration.
- Disabled sleeping, waiting, and fast travel during the exact freeze using
  xNVSE's per-mod control ownership.

## 0.1.1 - 2026-10-20

- Replaced the heavy visual treatment with a static The World color filter.
- Disabled activation/resume flashes, blur, double vision, and radial blur.
- Enabled the supplied custom activation and resume cues.
- Replaced the unreliable `PlaySoundFile` fallback with runtime Sound forms for
  JohnnyGuitar NVSE 5.16 and older.
- Corrected JIP Script Runner string expansion when assigning custom WAV paths.
- Added audio-synchronized smoothstep time ramps: 1.550 seconds to full freeze
  and 0.5825 seconds from resume cue to restored speed.
- Preserved the pre-activation global time multiplier and added immediate
  restoration for all safety-cleanup paths.
- Switched the custom cues to PCM WAV for reliable effect playback.
- Added and ran `tools/convert_audio.py`; cues are now 44.1 kHz mono PCM16.

## 0.1.0 - 2026-10-20

- Initial ESP-less implementation.
- Added configurable time stop, manual cancellation, duration, visuals, audio,
  deferred-projectile behavior through JIP FreezeTime, and lifecycle cleanup.
