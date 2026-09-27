# Version 1.0.1 — part fragments from guest kills — September 27, 2026

Cell stage only. Protocol remains 10; the server is compatible with Version 1.0.0.

- Fixed part-fragment selection for kills by the invited player. The native
  function rejected eligible parts when the NPC was outside the inner area of
  the owner's camera. Guest kill processing now bypasses that check without
  changing the camera position.
- Fragments spawn at the defeated NPC through the native loot table. Checks for
  parts present on the NPC, parts already unlocked, an active discovery pickup,
  and native drop probabilities remain in place. Parts are not awarded
  automatically: players still need to collect the fragment.
- Guest kill attribution survives delayed corpse breakup. Repeated removal does
  not leave that attribution attached to other NPCs or a new world.

- After stage completion, an open final History screen keeps the world loaded
  until the player closes it. Previously, the owner's departure could trigger
  the normal guest exit and interrupt History.
- Completion is also recognized when the connection closes before the next game
  update and the accepted-invitation flag has already been cleared.
- The ending screen and save request wait until History closes. Ordinary History
  viewing before stage completion retains the previous exit behavior.
- Passed 115 client and 154 isolated engine checks. The real loot selector
  reproduces the missing fragment and tests the fix for nine part types, rejects
  parts already unlocked or absent from the NPC, preserves native pickup
  retention, and keeps normal host behavior. Final object creation is replaced
  by a fixture in these tests.
- DLL build: 0 errors and 91 existing SDK/adapter warnings. Also passed 142 server
  checks, source invariants, and `git diff --check`.
- Fragment drops and collection in two windows, the ending screen, saving, and
  return to the menu still require gameplay verification. Automated tests do not
  replace that verification.

Installation: extract `spore-multiplayer-mod-v1.0.1-windows.zip` and update both
DLLs while the game processes are closed. The bundled server is unchanged from
Version 1.0.0.

---

# Version 1 — Cell stage only — September 27, 2026 (protocol 10)

The first major Spore Multiplayer Mod release supports **only SPORE's first
stage, Cell**. Creature, Tribal, Civilization, and Space multiplayer are not
supported yet. They are planned for future versions.

- Closing final History intercepts the native landfall transition. Instead of
  the second stage, a black screen displays: "The first stage is complete!
  The remaining stages are coming soon in the mod for SPORE on Steam."
- The screen identifies Version 1 and includes a "Main menu" button. The language
  follows the game settings, with English text for other locales.
- Completion is shared by both players. Players can finish reading and close
  an open History screen; ordinary History viewing does not end the stage.
- The owner requests a native Cell world save. The guest does not write a copy
  of the campaign. Entry to the second-stage editor is also blocked.
- Repeated completion events are safe, and a new invitation resets the ending
  screen. An already displayed ending survives the owner's departure.
- Includes all Beta 14–15 changes: species names, saves, guest entry and growth,
  respawn size, meat drops, and fixes for repeated NPC recreation.

Installation: close both windows, extract `spore-multiplayer-mod-v1.0.0-windows.zip`,
and run `Start-TwoSpore.ps1`. **Update both DLLs and the server**: protocol 10 is
incompatible with Beta 1–15. The supported setup is two windows on one PC,
Steam SPORE: Galactic Adventures, the verified March 2017 executable, and ModAPI
Launcher Kit. Complete play between separate computers remains unfinished.

Validated DLL/server builds, 142 server checks, 108 client checks, 116 checks of
executable functions, and source invariants. The real History callback, running
on isolated data, closes History before invoking the ending interception.
Ordinary History closure does not invoke that interception.

A full two-window playthrough, the ending screen's appearance, and saving during
this transition have not been verified in a running game. Version 1 identifies
the first-stage release scope; it does not confirm the absence of gameplay bugs.
The mod is distributed through GitHub; Steam refers to the target game version.

---

# Beta 15 — September 27, 2026 (protocol 9)

- Invited players receive shared progress immediately through native stage
  loading. Previous part discoveries, including spikes, and earlier growth are
  not replayed as new events. The old egg-entry sequence is finished to release
  input and the camera. New discoveries during gameplay still appear; local
  tutorial timers are not reset on every network update.
- Guest placement anchors coordinates to the owner's current position and
  scale, fixing the identified mismatch with the old spawn point after loading.
  Network creatures and food start with correctly sized physics bodies.
- Respawning uses the correct world-size factor: SpawnAvatar does not expect
  the visible cell scale. The living avatar's size and position are retained
  before its death animation. Avatar replacement preserves existing NPCs; the
  fallback full guest reset also restores shared growth.
- Breakup of an NPC killed by the guest is relayed to the owner and invokes the
  native meat loot table. Beta 14 called only the earlier death routine, which
  does not replace breakup into ordinary meat pieces.
- Background cells are no longer copied as interactive NPCs. Guest-side distance
  and scale culling of network replicas is blocked; world loading and removal by
  the owner retain native cleanup. This addresses identified causes of repeated
  replica recreation and lag.

DLL build: 0 errors and 91 existing SDK/adapter warnings. Passed 98 client checks,
110 isolated executable-function checks, including size calculations at different
growth levels and native meat-table selection during breakup, and source
invariants. The meat-table test does not count real objects in a running game.

Two-window verification is needed for joining a grown host, guest movement,
suppression of old notifications, guest death, and guest NPC kills. All reported
gameplay symptoms, including the sphere in the screenshot, have not yet been
confirmed resolved in an interactive session. Update both DLLs; the Beta 14 server
is compatible. Run `Start-TwoSpore.ps1` with both games closed.

---

# Beta 14 — September 26, 2026 (protocol 9)

- Species names are written directly to the model: native `cEditor::SetName`
  ignored changes when the campaign name was nonempty. Owner saves also write
  the name to `Games/Game0/SporeCoop-name-<gameID>.txt`; loading sends it with the
  initial progress of a new invitation.
- After loading a save, the guest applies shared growth and waits for body
  readiness before placement beside the owner. Physics nodes, the movement
  target, and camera position are updated.
- Lethal guest hits reach the owner, which invokes native NPC death with meat
  generation and animation. Repeated death does not generate a second drop.
- Cooperative avatar death replaces only the avatar. The shared NPC, food, and
  spawner pools are retained. Loading, single-player play, and live-avatar resets
  continue to use the full native procedure.
- Worlds copied to the second profile at launch are hidden while building its
  saved-game menu. The files and campaign records remain available to invitations.
  New worlds created in the second window after launch are not filtered.

Update both DLLs and the server together. Run `Start-TwoSpore.ps1` with both games
closed. A previously lost name cannot be recovered automatically: set it in the
editor and save the world as its owner. Preparing and backing up the second
profile remain part of the existing local workflow.

Validation: DLL/server builds, 134 server checks, 98 client checks, and 101
executable-function checks, including real NPC death and reproduction of the
native editor's refusal to change a campaign name. Two-window scenarios remain
unverified: reinviting after loading, meat from guest kills, owner death without
NPC restoration, name persistence across saving/restarting, and hiding copied
worlds from the second window's menu.

---

# Beta 13 — September 26, 2026 (protocol 8)

- Guest mating call: local NPC cleanup no longer deletes the native mate created
  by the call button. The game still controls movement and mating; editor entry
  is shared by both players.
- The species name is restored on the next editor visit. An empty cached-model
  name does not erase the shared name. Active input is written to the model
  before confirmation and is not overwritten while the UI closes.
- Guest plant and meat consumption events feed the shared History. Their diet
  counters are sent and applied first; the host then invokes the native history
  recorder. Duplicate events and packets for another world are rejected. Plant
  gains replay with the correct diet type, including native cell growth.
- Returning to the menu as world owner ends the session even if the game window
  stays open. The guest returns to the galaxy menu when the owner leaves or the
  connection is lost. Editor entry and loading do not count as leaving.
- Only the invitation owner writes the world. Guest save protection remains
  active throughout the exit transition; ending the world does not access the
  old cell pool. Saving a creature in the editor is still allowed.
- Retains Beta 12 changes and Beta 9–11 crash fixes.

Installation: close both game windows, extract
`spore-multiplayer-mod-beta-13-windows.zip`, and run `Start-TwoSpore.ps1`.
Update **both DLLs and the server**. Protocol 8 is incompatible with Beta 1–12.

Validated DLL/server builds, 128 server checks, 98 native checks, 89 executable-
function checks, and source checks. The isolated executable runs the real history
recorder, diet-path calculation, and mating-call availability handler. A full
two-window session has not been tested. Guest mating calls, names across
evolutions, guest diet contributions, owner-only saving, and automatic guest
exit need manual verification.

---

# Beta 12 — September 25, 2026 (protocol 7)

- Fixed the identified guest-mouth mismatch: the host creature was displayed
  over the guest's original cell, while feeding abilities still came from the
  old creature. After the shared model loads and is prepared, the mod now updates
  the actual avatar body and the campaign's creature key.
- Native functions replace the old graphics and physics nodes. The player object,
  health, progress, and collision record are preserved. The existing temporary
  appearance proxy remains in use until the shared model is ready.
- Added tests of real native functions on isolated data: releasing the old body
  without deleting the player and switching mouth abilities through the campaign
  creature key. Full function signatures are verified.
- Retains Beta 9–11 crash fixes. Protocol 7 is unchanged; the installed Beta 9–11
  server can be kept.

Installation: close both game windows, extract
`spore-multiplayer-mod-beta-12-windows.zip`, and run `Start-TwoSpore.ps1`.
Update the DLLs for **both** players.

Validated the DLL build, 116 server checks, 93 native checks, 75 executable-
function checks, and source checks. Guest collection of red meat pieces with a
carnivorous mouth still needs two-window verification; automated tests do not
reproduce an entire session.

---

# Beta 11 — September 25, 2026 (protocol 7)

- Refined Beta 10's editor-transition protection. If the network editor flag
  arrives before the local mode change, the peer cell and NPC replicas are
  removed normally from the still-active world. Once the local window leaves
  Cell, only references are reset, without accessing the freed pool.
- Protocol 7 is unchanged. The Beta 9–11 server is compatible; a server is included
  for a complete installation. All other Beta 9 features are retained.

Installation: close both game windows, extract
`spore-multiplayer-mod-beta-11-windows.zip`, and run `Start-TwoSpore.ps1`.
Update the DLLs for **both** players.

Validated the DLL build, 116 server checks, 93 native checks, 63 executable-
function checks, and source checks. Actual editor transitions and other scenarios
still require verification in two windows.

---

# Beta 10 — September 25, 2026 (protocol 7)

- Fixed the new 21:05:05 crash during the Cell-to-editor transition. During the
  mode change, the mod tried to find and remove a network cell by index from a
  native pool being freed. On leaving the world, it now resets peer, appearance-
  proxy, and NPC references without accessing that pool. Normal removal remains
  enabled during active gameplay.
- The cause was identified using the published Beta 9 PDB and game machine code:
  `CoopUpdate` → `RemoveRemoteCell` → `cObjectPool::GetIfNotDeleted`.
  This fix addresses that signature; the transition has not yet been tested in
  two game windows.
- Other Beta 9 features are retained. Protocol is unchanged; the archive includes
  a server for a complete installation, compatible with the Beta 9 server.

Installation: close both game windows, extract
`spore-multiplayer-mod-beta-10-windows.zip`, and run `Start-TwoSpore.ps1`.
Update the DLLs for **both** players.

Validated the DLL build, 116 server checks, 93 native checks, 63 executable-
function checks, and source checks. Gameplay, saves, and History thumbnails need
two-window verification.

---

# Beta 9 — September 25, 2026 (protocol 7)

- Added protection for the specific crash in two recent reports: the native
  loader tried to read a missing cell model through a null pointer. Loading is
  checked before creating a network replica and inside the native function,
  including deferred NPC loading. Missing models are skipped and creation
  retries are limited. This does not guarantee protection against other crashes.
- Network objects without a model override retain their original attachment
  type. Ordinary NPCs no longer become PlayerCreature merely because they were
  created through networking.
- The off-screen peer arrow is now bright yellow and reattaches to the current
  UI after transitions through the editor.
- Added synchronization of the owner's native History timeline: events,
  generations, and evolution-scale values. The guest receives a complete
  snapshot, applied before History opens. Pointers are not transmitted; shared
  model keys are mapped to local thumbnail keys.
- Invited players skip world writes, file preparation, and save cleanup. The
  owner is the invitation sender, regardless of host/guest network role. The
  protection survives disconnects until the world is left. Ordinary single-player
  play and saving a creature in the editor are not blocked.
- Retains player/NPC synchronization, shared growth, DNA, parts and missions,
  the editor, species name, evolution confirmation, appearance binding after
  evolution, and feeding-animation synchronization.

Installation: close both game windows, extract
`spore-multiplayer-mod-beta-9-windows.zip`, and run `Start-TwoSpore.ps1`.
Update **both DLLs and the server**: protocol 7 is incompatible with Beta 1–8.

Validated DLL/server builds, 116 server checks, 93 native checks, 63 executable-
function checks, and source checks. Added coverage for save ownership, guest
disconnects, timeline limits and structure, packet ordering, and native addresses.

Limitations: interactive two-window verification, Save and Quit, and History
thumbnails after several evolutions have not been tested. History updates before
opening; reopen an already visible window to see new events. When a required
model is missing locally, its creature may remain invisible instead of crashing.
This is a prerelease.

Diagnostics: `%TEMP%\SporeCoop.Probe.log`; look for `Prevented native E83EC8`,
`Guest campaign save skipped`, and `Verified native history recorder`.

---

# Beta 8 — September 25, 2026 (protocol 6)

- Fixed the path that substituted both players' appearance on the guest after
  shared evolution. Beta 7 logs showed successful confirmation, but the saved
  local model differed byte-for-byte from the host model. The mod created
  separate cached copies and hid the guest avatar even though both saved
  portraits looked identical.
- After applying the final shared model and successfully saving it natively,
  the mod remembers its relationship to the new local creature key. The peer
  uses that saved model; the local character is not replaced by a foreign-cache
  proxy. The save and avatar must first be verified to have matching keys.
- The player whose editor finishes automatically sends the same final shared
  model back, rather than the byte-modified result of the local save. This keeps
  appearance recognition working when the guest confirms first, too.
- The binding applies only to a specific world, connection, evolution session,
  final revision, and model content. New models, different avatars, cancellation,
  another evolution, and reconnection cannot reuse an old confirmation.
- Retains shared confirmation, species names, DNA/part synchronization, and the
  yellow arrow.

Installation: close both game windows, extract
`spore-multiplayer-mod-beta-8-windows.zip`, and run `Start-TwoSpore.ps1`.
Update the DLLs for **both** players. The server is included; protocol remains 6.

Validated the DLL build, 110 server checks, 87 native checks, 53 executable checks,
and source checks. Added 19 checks for binding the final model to its local save
and resetting that binding on state changes. The fix has not been visually
verified in two running windows; this is an experimental prerelease.

Diagnostics: `%TEMP%\SporeCoop.Probe.log`; look for
`Shared evolution appearance: native save bound to the final shared editor revision`
and `Shared evolution appearance: reusing the natively saved avatar for the peer`.

---

# Beta 7 — September 24, 2026 (protocol 6)

- Fixed the specific automatic-confirmation failure in Beta 6: EditorUI expects
  component activation (`0x287259F6`), while the mod sent a normal click (`0x17`).
  It now sends the correct event with save command `0x102`. When either player
  finishes, the other receives the final model, name, and DNA, waits for editor
  readiness, and invokes the native save.
- Local confirmation and cancellation are observed through the same event that
  EditorUI handles. Readiness checks and protection against delayed completion
  of an earlier session remain in place.
- The off-screen arrow is now yellow with a light outline.
- Retains previous synchronization of players, appearance, shared progress,
  parts, names, and DNA, as well as movement smoothing.

Installation: close both game windows, extract
`spore-multiplayer-mod-beta-7-windows.zip`, and run `Start-TwoSpore.ps1`.
Update the DLLs for **both** players. The server is included; protocol 6 is unchanged.

Validated the DLL build, 110 server checks, 68 native checks, 53 executable checks,
and source checks. A new test runs the real EditorUI handler and command switch
on isolated data: the old event is rejected, while the new event reaches the save
entry. It checks rejection for hidden, inactive, or busy editors and a successful
retry once ready. Only the save function is replaced by a test recorder, so the
test does not start the game or modify saves. Complete exit from both editors
has not yet been tested in two game windows for this version.

Diagnostics: `%TEMP%\SporeCoop.Probe.log`; `remote finish activation handled=1`
means the handler accepted the command, not that the entire save has completed.

---

# Beta 6 — September 24, 2026 (protocol 6)

- Fixed the automatic-confirmation path. Beta 5 logs showed the event being sent,
  but the host remained in the editor because the message went to a child button.
  The native command ID is now sent to EditorUI. If the handler is busy, the
  attempt waits; the final model must load before invocation.
- During evolution exit, new coordinates no longer use a model from an old
  appearance packet. After shared evolution, when the complete model matches
  the local avatar, the peer reuses that loaded appearance instead of a new cache
  under a foreign key. This addresses two identified substitution paths.
- Added a turquoise arrow at the screen edge pointing toward an off-screen peer.
  It accounts for the camera, scale, and aspect ratio, does not capture the mouse,
  and hides when the peer returns, during pause/editor use, or on disconnect.

Update the DLLs for **both** players. Extract
`spore-multiplayer-mod-beta-6-windows.zip`, close both game windows, and run
`Start-TwoSpore.ps1`. The server is included; protocol remains 6.

Validated the DLL build, 110 server checks, 68 native checks, and 39 executable
checks. Arrow projection is checked by reversing through SPORE's real camera
function, including rotation and off-screen points. Tests cover edges, corners,
wide and portrait windows, and invalid camera matrices.

Two-window testing started, but the user stopped control with Esc before testing
editor completion and the arrow during gameplay. Confirmation, appearance, and
arrow rendering still need a live session. Diagnostics: `%TEMP%\SporeCoop.Probe.log`;
look for `native EditorUI accepted remote finish command` and
`Peer appearance matches the live avatar; reusing its complete local skin`.

---

# Beta 5 — September 24, 2026 (protocol 6)

- Either player can send a species name with the creature revision and remaining
  DNA. The input field, caption, and model name saved by the game are updated.
  Cyrillic, quotation marks, and clearing a name are supported.
- After successful confirmation, the other editor receives the final body,
  name, and DNA, waits for parts to load and history to commit, then invokes the
  native confirmation button. The game's own validation remains in place.
- Editor cancellation is distinguished from confirmation. Repeated completion
  or messages from an earlier visit cannot close a new session or replace its model.
- Prevented guest reentry while waiting for the server's editor-exit response.
- Retains shared progress and parts, model/DNA exchange in the editor, player/NPC
  smoothing, and animation of the visible mouth.

Installation: close both game windows and update **both** DLLs and the server.
Extract `spore-multiplayer-mod-beta-5-windows.zip` and run `Start-TwoSpore.ps1`.
Protocol 6 is incompatible with Beta 1–4.

Validated DLL/server builds, 110 server checks, 67 native checks, 33 executable
checks, source checks, and a 17-second idle connection. Names and confirmation
have not yet been tested in two game windows; this is an experimental prerelease.

To verify in-game, rename the species in each window in turn, then finish the
editor as the host and, on the next evolution, as the guest. Both windows should
exit with the same name and creature. Diagnostics: `%TEMP%\SporeCoop.Probe.log`;
look for `EditorSync: remote final revision loaded; native Accept dispatched once.`

---

# Beta 4 — September 23, 2026 (protocol 5)

Fixes a Beta 3 regression. Logs confirmed that price validation rejected the shared
model, leaving the invited player with an empty body and blocking further edits.

- Removed cost recalculation from part properties. Remaining DNA is read from
  native editor history and sent with the model and its revision.
- Editor entry, adding/removing parts, and undo/redo use a shared model/budget
  snapshot. Older revisions cannot overwrite a newer DNA balance.
- Concurrent independent edits add local charges/refunds to the received budget.
  Acknowledged and identical edits are not charged twice.
- Replaced the mouth-animation call with one targeting visible AnimatedCreature
  objects, including PlayerCreature and RandomCreature. The previous engine
  call skipped those nested model types. The sender also reads the actual
  animation of the visible mouth.
- Retains player smoothing and previous shared-world/progress fixes.

Update **both** players' DLLs and the server: protocol 5 is incompatible with
Beta 1–3. Close both windows, extract `spore-multiplayer-mod-beta-4-windows.zip`,
and run `Start-TwoSpore.ps1`.

Validated DLL/server builds, 100 server checks, 63 native checks, 33 executable
checks, and source checks. Additional regressions cover the 16 → 6 budget change,
DNA refunds, old/incomplete packets, and concurrent edits. Two-window testing
started, but the user stopped control with Esc before entering the world. Editor,
DNA, and mouth behavior remain unverified in live gameplay for this release.

Diagnostics: `%TEMP%\SporeCoop.Probe.log`; look for `EditorSync` and `MouthSync`.

---

# Beta 3 — September 23, 2026 (protocol 4)

## DNA in the shared editor

- Receiving the other player's changes now updates the native editor budget:
  adding parts spends DNA; removing parts and undoing purchases refund it. This
  addresses the screenshot where adding spikes left one window at 6 and the
  other still at 16.
- A symmetric pair counts as one purchase. Repeated model snapshots and color
  changes do not spend DNA again. Editor history preserves the updated budget.
- Prices come from part properties before the new model loads, without reading
  its still-unloaded cost fields. The initial body is not charged again on first
  entry; edits received during the transition are accounted for.
- A merged edit that exceeds the DNA budget is not applied; the local model is
  retained. Concurrent purchases with almost no DNA remaining may require
  removing a part before synchronization can resume.

## Smoothing and animation

- Network-cell position and full rotation are interpolated each frame with a
  75 ms packet buffer. Teleports, body changes, and disconnects reset the history.
  There is no extrapolation beyond the last known position; local input does
  not use this buffer.
- Added experimental native feeding and chewing animation on network cells and
  the guest's appearance proxy. Repeated packets do not restart the same gesture
  every frame. This is visual synchronization; food is not credited again.
- The animation call is checked against the supported executable signature.

## Validation and installation

Built the DLL and server. Passed 94 server checks, 59 top-level native checks
with additional DNA, symmetry, animation, and per-frame smoothing scenarios,
33 executable checks including 1000 movement frames, source checks, and a
17-second idle connection. These checks do not replace two-window gameplay:
the DNA, mouth, and smoothing changes have not yet been verified interactively.

Close both SPORE windows, extract `spore-multiplayer-mod-beta-3-windows.zip`,
and run `Start-TwoSpore.ps1`. Update both players' DLLs. This release also contains
all Beta 2 changes: shared world, growth, missions and parts, editor entry,
appearance/edit transfer, NPC smoothing, and host-departure notification.
Their detailed description is retained below.

Diagnostics: `%TEMP%\SporeCoop.Probe.log`; `EditorSync: shared budget` reports
the applied cost and remaining DNA.

---

# Beta 2 — September 23, 2026 (protocol 4)

Experimental release for the Cell stage of SPORE Galactic Adventures. Update
both windows' DLLs and the server together. Incompatible with Beta 1 / protocol 3.

## Player and world synchronization

- Network cells move and rotate through native engine functions that update
  physics nodes with the visible model. Fixed the path that returned cells to
  their old coordinates after physics updates.
- Growth accounts for world scale and offset relative to each player's camera.
  Players and objects are transmitted in shared coordinates.
- Complete appearance, colors, the selected model, and creature size are sent.
  A hidden original cell is restored if its appearance proxy loses its graphics.
- Removed accidental death-state activation on network replicas. Native engine
  functions exclude visual player duplicates from collisions.
- Expanded world-object transfer to NPCs, food, parts, and scenery, with health,
  animation, death state, damage requests, and removal requests. The invitation
  owner applies authoritative world changes.
- NPC position and rotation are smoothed between snapshots sent every 100 ms.
  Teleports or long gaps reset the smoothing history.

## Shared progress and editor

- Food gains, discoveries, and missions use acknowledged events so an older
  snapshot cannot erase local progress or award it again.
- Applying shared progress invokes native growth, part notifications, mission
  updates, and the first cinematic. Temporary cinematic actors are protected
  from NPC-synchronization cleanup.
- The second window opens the Cell editor through the native campaign transition.
  The shared model is explicitly applied after entry, avoiding the green starter body.
- Model replacement uses a separate object: reattaching the current object
  cleared its body. History is committed after the parts load.
- Completed edits are sent from editor history, including undo and redo.
  Hovering over a part handle no longer blocks transmission.
- Added model revisions, acknowledgements, and merging of independent edits.
  Fixed the reinvitation bug identified in logs: the client kept an old revision,
  ignored the new model, and repeatedly sent edits rejected by the server,
  leaving spikes visible in only one window.

## Exit and launch

- Host exit or crash sends "Host left" to the other player. Connection teardown
  preserves that reason and clears network state.
- Strengthened path validation before updating the isolated second profile.
  Backups receive unique timestamps.
- Added diagnostics for editor readiness, revisions, sent edits, physics
  replicas, movement, and engine compatibility.

## Validation and limitations

Passed 94 server protocol checks, 59 native client checks, 32 engine/ABI checks
(including 1000 movements), source invariants, and a 17-second connection
without game input. Rebuilt the DLL and server. Some tests include additional
checks for smoothing and model merging.

The latest screenshot confirms matching initial creatures in both editors,
but shows the earlier spike-transfer failure. Resetting the revision counter
after a new invitation is covered by an automated test; it has not yet been
retested in two game windows. Full combat, cinematic, editor-exit, and stage-
transition synchronization remains experimental. Cross-PC play and complete
world transfer have not been verified.

## Installation

1. Close both SPORE windows.
2. Extract `spore-multiplayer-mod-beta-2-windows.zip`, keeping the `bin` folder.
3. Run `Start-TwoSpore.ps1`; it installs the DLLs for both launchers.
4. Load the Cell stage, send an invitation through Esc, and accept it.
5. Check growth, part collection, editor entry, and edits in each window.

Requires SPORE Galactic Adventures, ModAPI Launcher Kit, and Windows. The scripts
target two local windows, a game installation at `C:\Games\SPORE Collection`, and
two launcher folders under `C:\ProgramData`. Adjust paths for other installations.
Standard Launcher Kit dependencies and Visual C++ Runtime x86 are also required.
Diagnostics: `%TEMP%\SporeCoop.Probe.log`. The archive does not include the game,
Launcher Kit, user saves, or account settings.

---

# Development history — September 23 Cell progression/editor fix

- Follow-up after the two-window editor report: explicitly install the shared
  body after guest editor activation; a cached creation key alone can fall back
  to the native green starter body. Do not acknowledge the initial snapshot
  before applying it.
- Publish completed native history transactions using the one-based history
  cursor, including undo/redo. A hovered handle no longer blocks sending a
  committed part or receiving an update while the mouse button is released.
  Add per-window `EditorSync` readiness, history and packet diagnostics. These
  follow-up changes pass offline checks; live two-window validation is pending.
- Finish native food/growth, part-unlock/quest notifications and first-part
  cinematic replay instead of only writing shared save counters.
- Open the guest's Cell campaign editor through the verified native entry with
  the complete shared body and paint cached before the transition.
- Replace editor models with a separate loaded object: reattaching the active
  object disposes its own body. Wait for body readiness before publishing or
  committing history, preserve names, and ignore the previous editor visit's
  snapshot while waiting for a new entry acknowledgement.
- Interpolate NPC positions and planar rotations in fixed world coordinates;
  publish every 100 ms, reset on teleports and stop at the last received pose.
  Preserve native cinematic actors instead of deleting them as guest NPCs.
- Deliver and retain the host-left reason before disconnecting the peer.
- Validation: native/protocol regressions, executable fingerprints for the four
  progression/editor entry points, movement/collision fixtures and DLL build.
  A complete two-window growth/unlock/editor gameplay run remains unverified.

# Development history — September 22 replica state fix

- Stop marking synthetic cells as dead (`field_112`), which enables NPC death
  animation and the native `cell_death_continuous` effect. Remove the ineffective
  hiding of structure effect FC1CD6A3.
- Unregister synthetic collision bodies through the verified engine broadphase
  removal routine; retain complete graphics and native body movement.
- Compare the actual avatar's full appearance before creating a guest proxy.
- Add real engine collision-removal regressions and death/collider diagnostics.
  Visual disappearance of dust and guest drift still require session validation.

# Development history — September 21 movement fix

- Move and rotate articulated physics bodies through verified native engine
  routines, fixing the transform-only path that snapped clones back to their
  old physical positions.
- Apply the same fix to guest join placement, guest appearance proxies, and NPC
  mirrors. Reapply existing network bodies just before Cell graphics update.
- Restore the guest's original visibility if its appearance proxy loses graphics.
- Log actual graphics coordinates separately from simulation coordinates.
- Validate movement entry points with ASLR-aware executable fingerprints.
- Add an offline regression using the installed engine's movement routines,
  including 1000 repeated movements. Full gameplay validation is still manual;
  this local build has not been published as a new release.
- Keep launcher creation-library backups timestamped even when the source has
  no Games directory; validate isolated replacement paths before removal.

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
