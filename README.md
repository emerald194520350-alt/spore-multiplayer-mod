# Spore Multiplayer Mod — Version 1.0.1

**Только первый этап — «Клетка». / Cell stage only.**

SporeCoop is a community co-op mod for the Steam edition of **SPORE: Galactic Adventures** on Windows. Version 1 covers the first stage, **Cell**. Creature, Tribal, Civilization and Space multiplayer are not included. Later stages are planned for future versions.

The supported launch workflow runs two game windows on one PC with separate profiles. Remote LAN/VPN play and complete world transfer between computers are still unfinished.

## End of the first stage

Finish Cell and close the final History screen as usual. The mod intercepts the transition before landfall and the Creature editor, then shows a black screen:

> Первый этап завершён!
>
> Остальные этапы скоро появятся
>
> в моде для SPORE в Steam.

The screen also identifies **Version 1 — этап «Клетка»** and has a **Main menu / В главное меню** button. An English version is shown for other game locales. Both players receive completion; a player still viewing History can close it first. The owner requests a native Cell save; the invited player's campaign remains protected from saving. Normal History viewing during Cell does not end the game.

The Steam wording describes the target game and planned updates. This repository distributes the mod through GitHub releases.

## Included in Version 1

- Version 1.0.1: guest kills can drop eligible part fragments outside the owner's camera. Native part eligibility and loot probabilities are preserved.
- Version 1.0.1: the guest can finish reading final History after the owner leaves, then reach the ending screen.
- Invitations from the pause menu, with either window able to own the selected world.
- Shared Cell growth, DNA, parts, diet history, species name and creature editor.
- Player movement and appearance, NPC replication and shared object interactions.
- Guest entry at the shared growth level without replaying old unlock notifications.
- Corrected avatar respawn size, guest corpse breakup and replica scale/culling from Beta 15.
- Owner-only campaign saves, saved species names and guest exit when the owner leaves.
- A clear ending after Cell, with further native stage progression blocked in the co-op launch.

## Install and run

Requirements: Windows, the Steam Galactic Adventures executable supported by Spore ModAPI, and **SPORE ModAPI Launcher Kit**. This build targets the Steam March 2017 executable. Base SPORE alone is insufficient.

1. Close both SPORE windows.
2. Download and extract `spore-multiplayer-mod-v1.0.1-windows.zip` from [Releases](https://github.com/emerald194520350-alt/spore-multiplayer-mod/releases).
3. Run `Start-TwoSpore.ps1`. On an existing installation, the desktop shortcut **SPORE Coop - 2 окна** runs the same workflow.
4. Load a Cell world in one window and invite the other player through the pause menu. Accept in the second window.

**Update both DLLs and the server together. Version 1 uses protocol 10 and is incompatible with Beta 1–15.** The launcher installs the DLLs into both Launcher Kit directories and runs the matching server.

Version 1.0.1 keeps protocol 10 and the Version 1.0.0 server. Update both client DLLs to receive the fixes.

The second profile is `%AppData%\SporeCoop2`. The launcher prepares copies of the first profile's saved games and backs up the second profile before copying. Prepared copies are hidden from the second window's galaxy menu but remain available for invitations. The guest loader currently chooses the newest prepared campaign; use that campaign for the local session.

## Verification and remaining limits

DLL/server builds and automated checks pass. The tests cover network state, native message parsing, growth coordinates, editor synchronization, native size calculations, meat-table selection and the actual History-close callback from the installed executable. They do not constitute a full two-player playthrough.

Version 1.0.1 also executes the native loot selector with part pickups: it reproduces the owner-camera rejection and checks the fix for nine part types, already unlocked parts, missing parts and native pickup retention. This uses an isolated executable image with a substituted spawn sink.

The new fragment-drop fix, final ending screen, native save/return-to-menu flow and the Beta 15 gameplay fixes still need interactive verification in two windows. Visual layout has not been confirmed in a live game. Rare save states, tutorials and unusual editor transitions may still expose defects. Version 1 names the Cell-only release scope; it does not claim support for the rest of SPORE.

See [RELEASE_NOTES.md](RELEASE_NOTES.md) for current changes and archived beta history.

## Build and test

Building requires Visual Studio C++ tools (MSVC v143 and Windows SDK), .NET Framework 4 and the included Spore ModAPI source. Node.js 22 or newer runs the server/source tests.

```powershell
.\Build-Probe.ps1 -LauncherRoot 'C:\ProgramData\SPORE ModAPI Launcher Kit'
.\Build-Server.ps1
.\Test-Server.ps1
.\Test-Native.ps1 -GameExecutable 'C:\Games\SPORE Collection\SporebinEP1\SporeApp.exe'
node .\tests\native-source.test.mjs
```

The engine tests map a private executable image without starting the game. Binaries, symbols, local saves and temporary files are excluded from Git. The release archive includes the runtime files and third-party notices.

For a bug report, provide `%TEMP%\SporeCoop.Probe.log`, the version and the last action in each window. `coopStatus` writes current diagnostic state; `coopJoin` and `coopSpawn` are diagnostic commands and are not required for ordinary invitations.

## License

The original SporeCoop source is covered by [LICENSE](LICENSE). Third-party components, including Spore ModAPI and its dependencies, retain their own licenses; the binary distribution includes their notices and license texts.

SporeCoop is an independent community project, not affiliated with or endorsed by Electronic Arts, Maxis or Valve.
