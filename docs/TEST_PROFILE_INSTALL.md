# Test profile installation

The prepared MO2 mod directory is:

`C:\Modlists\VNV\mods\The World NV`

No ESP plugin entry is needed. Before packaging or enabling the mod, build the
included Win32 project as described in `native\TheWorldNV\README.md` and place
`TheWorldNV.dll` in the mod's `NVSE\Plugins` directory. If it is absent, the
ability prints an error and does not activate.

To try it later, open MO2, select `Teste_Traducao`, confirm `The World NV` is
checked, launch the existing NVSE-enabled game entry, load a save, and press H.
Press H again to resume early. Otherwise The World ends after 10 seconds or
when AP reaches zero. It consumes 12 AP per real gameplay second, prevents AP
regeneration while active, and then enforces a 20-second recovery cooldown.

For a focused hit check, fire conventional hitscan, beam, shotgun, automatic,
explosive, grenade, melee, and slow-projectile weapons while time is stopped.
Check that health, limbs, hit reactions, deaths, XP, hit events, and notifications
do not change before resume, then resolve once immediately after normal resume.
Hitscan tracers are cosmetic and need not reach the target before the queued
engine transaction is released. Do not use a load door while expecting the
freeze to persist; the safety handler resumes time and discards the queue first.

For perception, start in combat with an enemy facing the player, freeze, move
behind it, and resume. It should search the old position during the configured
1.75-second concealment window, then reacquire normally if it can detect the
player. Confirm that the player is not forced to crouch. For audio, check music
pause/resume and confirm voice/radio volumes return to their exact prior values.

No game or editor launch was performed during preparation.
