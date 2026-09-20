# spore multiplayer mod beta 1

Experimental Windows build for SPORE Galactic Adventures Cell Stage. Both game
instances and the session server must use this release (protocol 3).

## Changes

- Either window can own the invitation, creature appearance, scale and shared pause.
- Fixed invisible peers when a cell is drawn through structure attachments.
- Native cells use simulation coordinates and scale without applying a nested model's scale twice.
- Removed redundant local appearance proxies for identical creatures and the blocking standalone-bake requirement.
- NPC replication carries the selected creature model, with the nearest 48 creatures included in each snapshot.
- NPC and player-clone removal uses complete engine cleanup, guarded for the supported executable ABI.
- Independent, acknowledged food gains and shared part unlocks survive simultaneous pickups and delayed snapshots.
- Movement traffic no longer evicts queued inventory or invitation events.
- New invitations reset progress acknowledgements; reconnects restore local appearance and release the mod's pause.
- The launcher updates both DLLs and the server together and refuses to mix builds while old windows are running.

## Install and test

1. Install SPORE Galactic Adventures and Spore ModAPI Launcher Kit first.
2. Close both game windows before replacing an older mod version.
3. Extract `spore-multiplayer-mod-beta-1-windows.zip` into one folder, preserving `bin/`.
4. Run `Start-TwoSpore.ps1`, or `Start-TwoSpore.ps1 -TraceMovement` for diagnostics.
5. Load a Cell Stage save in one window, invite the other player from Esc, and accept in the second window.

The supplied local launcher targets the tested installation at
`C:\Games\SPORE Collection` and two Launcher Kit folders under `C:\ProgramData`.
Adjust the launcher paths for another installation. It backs up and refreshes
the second profile from the first before starting both games. The game and the
Launcher Kit are not included in the release archive.

## Validation and known issues

The DLL and server build successfully. Automated validation passed 84 server
protocol assertions, 53 native assertions, source invariants, and a 17-second
native-client idle connection test. Both windows were launched with the release
DLL. Full gameplay validation remains in progress; these tests do not drive the
SPORE simulation. A live two-window smoke test shows matching shared food/part
counters, but native peer cells can still drift away from received coordinates
and NPC replication needs further in-game validation. Movement should therefore
be treated as experimental, not as fully resolved in this beta.

NPC health, combat, food objects and the entire world simulation are not yet
authoritative across both windows. Mirrored NPCs in the joining window remain
invulnerable. Other stages and editor transitions need further gameplay testing.
Cross-PC Radmin VPN play and complete world transfer are not validated. Network
cell creation is disabled if the engine cleanup signature is unsupported.

Diagnostics: `%TEMP%\SporeCoop.Probe.log`. The repository contains the source and
build scripts; the Windows archive contains the compiled mod, server and launch
scripts, without game saves or private configuration.
