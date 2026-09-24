# spore multiplayer mod beta 7

SporeCoop is an experimental local co-op mod for SPORE. It is designed to let two SPORE instances on the same PC share a game session, with a future LAN/Radmin VPN mode planned for remote players.

The project is currently **Beta 7** (protocol 6), an experimental prerelease. Update both game DLLs: this version corrects the event type that caused Beta 6 to ignore remote editor completion and makes the off-screen teammate arrow yellow. The actual native editor handler and command dispatcher are covered by a regression test; a complete two-window save/exit still needs in-game verification. The server protocol is unchanged from Beta 5; Beta 1–4 cannot join this build. Crashes, visual differences, desynchronisation, and unsupported stages are still possible. See [release notes](RELEASE_NOTES.md) for changes, validation and installation.

Beta 2 adds native articulated-body movement, growth-aware world coordinates,
shared object interactions and NPC interpolation. Cell progress now invokes
native growth, part/quest notifications and the first-part cinematic. Both
editors load the shared species; completed edits, undo/redo and concurrent edits
use revisioned synchronisation. The latest fix resets those revisions for a new
invitation, preventing endless rejected edits after an earlier session. The
remaining player receives a host-left message when the host exits or crashes.

Beta 4 fixes the Beta 3 pricing regression that blocked shared editor model
loading. The native DNA balance now travels with each model revision instead
of being reconstructed from part properties. Both the initial body and later
edits use this model/budget pair, with budget rebasing for concurrent edits.

Mouth playback now targets the visible AnimatedCreature objects, including
PlayerCreature and RandomCreature attachments skipped by the previous native
animation call. The sender also reads actual mouth animation groups from the
visible model. Beta 3's 75 ms player interpolation is retained.

The Beta 3 failure was confirmed in the game log. Beta 4 builds and automated
regressions pass. An interactive two-window retest was started but stopped by
the user with Escape; editor/DNA and mouth behavior still need in-game validation.

## What the mod is for

The mod explores how two players can experience the same SPORE world together. The host and guest instances communicate through a small TCP session server and exchange gameplay state.

Current experimental goals include:

- inviting a second player from the in-game pause menu;
- accepting an invitation and joining the host's saved world;
- displaying a network clone of the other player;
- synchronising creature appearance, position, scale, and species data;
- sharing cell-stage progress, food progress, unlocked parts, and editor state;
- running two isolated SPORE profiles and two ModAPI instances on one PC;
- providing diagnostic console commands such as `coopStatus`, `coopSpawn`, and `coopJoin`.

## Beta 4 limitations

This version has not been validated across every SPORE stage. The protocol and server are tested automatically, but the tests do not drive the SPORE game engine. Creature-stage transitions, tutorials, quests, editor transitions, and unusual save states may still cause crashes or incomplete synchronisation. Remote play over Radmin VPN/ZeroTier/Hamachi is prepared at the protocol level, but complete world transfer and reliable cross-machine testing are not finished.

## Requirements

- Windows and a working SPORE installation;
- SPORE ModAPI Launcher Kit;
- the same mod build in both game instances;
- PowerShell 5.1 or newer;
- Visual Studio build tools if rebuilding the native DLL.

## Running two local instances

Use:

```powershell
.\Start-TwoSpore.ps1
```

This creates/uses the isolated second profile at `%AppData%\SporeCoop2`, installs the latest DLL in both Launcher Kit folders, starts the session server, and launches host and guest instances. The desktop shortcut `SPORE Coop - 2 окна` runs the same workflow.

The host loads a saved world and uses the pause menu's co-op invitation. The guest accepts the invitation; the guest profile then attempts to load a fresh copy of the host world. `coopJoin` remains available as a diagnostic fallback. `coopSpawn` only creates a manual diagnostic clone and is not required for normal joining.

## Testing

### Beta 2 — September 23 (protocol 4)

This build implements native shared growth, unlock/quest notifications,
first-part cinematic replay and guest entry into the Cell campaign editor. It
also replaces editor models without disposing the incoming body, waits for body
loading before saving history, and interpolates NPC movement between snapshots.
Both DLLs and the server must be updated together using `Start-TwoSpore.ps1`.

Automated checks cover protocol handling, editor revision reset on a new invitation, interpolation, shared progress,
executable entry-point fingerprints and native movement/collision fixtures.
Full two-window gameplay is still unverified. Retest growth, a spike pickup and
the 1/6 quest counter, the cinematic, then editor entry/body/paint and return to
the world. Exiting or crashing the host should show “Хост вышел” to the peer.

### Earlier development notes — September 21 movement fix

Cell movement now uses the game's native position and orientation setters. A
cell's articulated physics nodes must move together with its object transform;
otherwise the next simulation update derives the old position from those nodes.
This applies to the remote player, the joining player's appearance proxy, join
placement, and mirrored NPCs. Existing network bodies are corrected after native
simulation, immediately before Cell graphics update. The local avatar keeps its
native input and camera ownership.

The engine entry points are checked against the tested executable, including ASLR
relocations. Unsupported builds disable network cell creation. Movement logging
now reads actual model positions after the graphics update; unavailable graphics
are marked `gfxReady=0` instead of repeating simulation coordinates.

Run the movement regression with the installed game executable:

```powershell
.\Test-Native.ps1 -GameExecutable 'C:\Games\SPORE Collection\SporebinEP1\SporeApp.exe'
```

This maps the executable without starting it and runs only verified movement
routines against fixture data. It reproduces the transform-only failure and
checks translation, rotation, unrelated nodes, and 1000 consecutive movements.
It does not validate a live game session. The two-window gameplay check remains
manual: move each player, reverse the invitation direction, grow, enter/exit the
editor, pause/resume, and disconnect the peer.

### September 20 sync build (protocol 3)

The invitation sender now owns appearance and size in either window. Native
cells publish their simulation transform and visibility even when the renderer
uses structure attachments. Identical local species do not need an extra hidden
avatar proxy, and native clones no longer wait for an unrelated standalone bake.

NPC snapshots include the selected model key as well as the cell resource, so a
random-creature resource is not rerolled independently in the other window.
The nearest 48 creatures are mirrored. Cell removal uses the game's complete
cleanup function after validating the installed executable's native ABI; unknown
executables disable network cell creation instead of freeing only the pool slot.

Food gains are now separate, acknowledged increments for each player. Parts
merge into a common inventory; unacknowledged local gains survive older incoming
snapshots. This requires protocol 3 in both DLLs and the server.

Close **both** running game windows, then run `Start-TwoSpore.ps1` (or the usual
two-window shortcut). It installs the DLLs and any `SporeCoop.Server.next.exe`
update together. Use `Start-TwoSpore.ps1 -TraceMovement` for per-window diagnostic
logs. The launcher refuses an update while old game windows are still running.

This build has passed automated native and server tests, but its final gameplay
verification is pending. NPC health, combat outcomes, food objects and the full
world simulation are not yet one authoritative shared simulation. Mirrored NPCs
remain invulnerable in the joining window. LAN/Radmin play still requires matching
assets and prepared saves on both machines.

Run the protocol tests with:

```powershell
.\Test-Server.ps1
.\Test-Native.ps1
node .\tests\native-source.test.mjs
```

`Test-Native.ps1` builds a Win32 test executable with Visual Studio C++ tools.
It exercises the native network message parser and visual-state decisions:
signed SPORE pool handles, appearance/position identity, reconnects, owner-led
size, and local opacity overrides. These checks do not run the game engine.

For a Cell visual regression check, accept an invitation, move both players,
grow by eating food, and edit the creature before returning to the world.
Repeat with the other window sending the invitation. Both windows should show
the owner's creature and size, with separate local and remote visuals. Closing
the peer or entering the editor must restore any locally hidden original cell.
Run `coopStatus` in each game's cheat console to write role, owner, cell handles,
scale, target size, opacity and appearance readiness to `%TEMP%\SporeCoop.Probe.log`.

The joining player's appearance is still a visual proxy: its underlying cell
continues to own input and collision. Matching parts and colours does not yet
guarantee matching collision or part abilities across different saved creatures.

To rebuild the native probe DLL:

```powershell
.\Build-Probe.ps1 -LauncherRoot 'C:\ProgramData\SPORE ModAPI Launcher Kit'
```

When reporting a problem, include `%TEMP%\SporeCoop.Probe.log` and the exact last action. Useful reports include crashes during growth, duplicate clones, mismatched colours/parts, failed invitations, or quests that do not update for both players.

## Repository status

This repository contains the mod source, session server, launch scripts, tests, and the Spore ModAPI headers used to build it. Generated binaries, local save data, registry backups, and temporary build output are intentionally excluded.

SporeCoop is an independent community project and is not affiliated with or endorsed by Electronic Arts or Maxis.

## License

The original SporeCoop source code in this repository is released under the [MIT License](LICENSE). Third-party components, including Spore ModAPI and its bundled dependencies, remain subject to their respective licenses.
