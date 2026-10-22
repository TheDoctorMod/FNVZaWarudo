# Native hit interceptor

This xNVSE plugin intercepts all six FalloutNV 1.4.0.525 call sites for
`Actor::HitMe`. While The World owns the exact freeze, complete player-originated
`ActorHitData` transactions are retained before the engine can change health,
limbs, fatigue, death state, XP, hit events, or notifications.

Normal resume replays each transaction once on the following main-loop tick.
Safety cancellation, load/new game, and exit to the main menu discard the queue
because its engine pointers must never cross those lifecycle boundaries. The
queue has a hard limit of 4096 hits; overflowed hits are suppressed rather than
allowed to apply during stopped time.

Attacker identity is retained separately from the copied `ActorHitData`. Before
each replay, the plugin restores both `source` and `target`, resolves the saved
projectile or explosion by FormID, and accepts it only if the resolved pointer
and class still match the original object. A valid cause has its `sourceRef`
restored to the original attacker. An expired or recycled cause pointer is
cleared so it cannot make an unrelated actor appear responsible for the hit.

The plugin also chains both the `HandleTracer` and `UpdateProjectile` vtable
slots for the six New Vegas projectile classes. Player-owned projectile visual
updates are skipped while frozen and continue through the original
implementation after resume. Hooking both is necessary because ordinary
hitscan bullet trails advance through `UpdateProjectile`, while beam/flame
classes have specialized `HandleTracer` implementations. Unlike hit
interception, this also covers missed shots, which have no `ActorHitData`.
The projectile 3D is still allowed to load at its spawn transform. Suppressing
its update leaves a projectile model that provides one suspended bullet at its
origin, when that projectile form has a visible model, without advancing or
creating a continuous trail. Its original update resumes with time.

The DLL synchronously dispatches `TheWorldNV:BeforeHitsReplayed`, runs every
queued `HitMe` continuation, and then dispatches `TheWorldNV:HitsReplayed` in
the same main-loop callback. The first event briefly restores normal player
detectability for attribution. The completion event locks both prior enemies
and witnesses made hostile by the replay before reapplying concealment. No AI
frame runs between those two detection changes.

## Building

The project is Win32-only, uses C++20 as required by the current xNVSE headers,
and targets the standard FalloutNV 1.4.0.525 runtime.
It needs an xNVSE **source checkout**; the installed game files do not contain
the development headers.

Set the MSBuild property `XNVSE_ROOT` to either:

- the repository root, where `nvse\nvse\prefix.h` exists; or
- its first `nvse` directory, where `nvse\prefix.h` exists.

The project normalizes either form to the repository root and adds both that
directory and its `nvse` child to the compiler search path. Both are necessary:
`nvse\prefix.h` includes `common\IPrefix.h` from the repository root.

For this development workspace, the project also automatically detects the
existing checkout at
`%LOCALAPPDATA%\Temp\tw_nvse_research\xNVSE`. An explicitly supplied
`XNVSE_ROOT` always takes precedence over autodetection.

For example, from a Developer PowerShell:

```powershell
msbuild .\native\TheWorldNV\TheWorldNV.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:XNVSE_ROOT=C:\Source\xNVSE
```

When building inside Visual Studio, create a user property named `XNVSE_ROOT`
with the same absolute path, or start Visual Studio from a shell where the
`XNVSE_ROOT` environment variable is already defined. The project now stops
with a specific configuration error if neither supported directory layout is
found.

After building the Release/Win32 configuration, copy `TheWorldNV.dll` from
`native\TheWorldNV\bin\Release` to
`mod\NVSE\Plugins\TheWorldNV.dll` before packaging the mod.

The scripts communicate with the DLL through three private, zero-argument NVSE
events, so the plugin consumes no community opcode range. Its call hooks retain
the previously installed destination per site, allowing another well-behaved
`Actor::HitMe` call hook to remain in the chain regardless of plugin load order.
