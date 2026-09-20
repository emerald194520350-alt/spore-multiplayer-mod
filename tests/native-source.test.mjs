// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const project = dirname(dirname(fileURLToPath(import.meta.url)));
const probe = await readFile(join(project, 'native', 'Probe.cpp'), 'utf8');
const net = await readFile(join(project, 'native', 'CoopNet.cpp'), 'utf8');
const ui = await readFile(join(project, 'native', 'CoopUi.h'), 'utf8');

assert.doesNotMatch(ui, /graphics\.GetColor\(/,
  'SDK Graphics2D::GetColor has an incompatible structure-return ABI and corrupts the paint stack');
assert.match(ui, /ReadGraphicsColor\(graphics\)/,
  'Custom drawing must preserve color through the native scalar-return ABI');

function body(source, signature, nextSignature) {
  const start = source.indexOf(signature);
  const end = source.indexOf(nextSignature, start + signature.length);
  assert.ok(start >= 0 && end > start, `Unable to isolate ${signature}`);
  return source.slice(start, end);
}

const updateRemote = body(probe, 'void UpdateRemoteCell(', 'std::string Base64Encode(');
const updateStability = body(probe, 'bool UpdateLocalCellStability(', 'void SubmitLocalAppearance(');
const submitAppearance = body(probe, 'void SubmitLocalAppearance(', 'void ApplyRemoteAppearance(');
const applyRemoteAppearance = body(probe, 'void ApplyRemoteAppearance(', 'void UpdateRemoteCell(');
const coopUpdate = body(probe, 'void CoopUpdate()', 'void Spawn()');
const inviteUI = body(probe, 'void UpdateInviteUI(', 'void CoopUpdate()');
const validateSavedWorld = body(probe, 'bool ValidateIncomingSavedWorld(', 'bool FindNewestSavedGame(');
const sharedEditor = body(probe, 'void UpdateSharedEditor(', 'constexpr uint32_t kInviteButtonID');
const stateHandler = body(net, 'if (type == "state")', 'if (type == "editorOpen")');
const readPose = body(probe, 'CoopNet::CellPose ReadPlayerRenderPose(', 'CoopNet::CellProgress ReadCellProgress(');
assert.doesNotMatch(readPose, /GetModel\(\)|model &&/,
  'Cells rendered through structure attachments must still publish visible movement');
assert.doesNotMatch(probe, /mCells\.DeleteObject\(/,
  'Cell removal must clean up native graphics and spatial queries, not only the pool slot');
assert.doesNotMatch(updateRemote, /IsBaked\(visualKey/,
  'A playable native cell must not wait on an unrelated standalone mesh bake');
assert.match(probe, /GetRemoveCellFunction\(\)[\s\S]*MatchesRemovalAbi/,
  'The engine-specific cell destructor must validate its calling convention and targets');

assert.match(net, /std::uint64_t gSpeciesSequence = 1;/,
  'The first appearance sequence must be newer than the zero-initialized receiver state');
assert.match(net, /constexpr int kProtocol = 3;/,
  'The native client must reject older incompatible protocol builds');
assert.doesNotMatch(updateRemote,
  /ResourceKey\(snapshot\.remoteModelInstance[\s\S]*snapshot\.remoteModelGroup\)/,
  'A remote visual must never fall back to an uncached foreign resource key');
assert.match(updateRemote, /gRemoteCellIndex = CreatePlayerCellClone\([\s\S]*game->mCells\.GetIfNotDeleted\(gRemoteCellIndex\)/,
  'The peer must be a complete native cell object rather than a detached eyes-only model');
assert.doesNotMatch(updateRemote, /LoadCreature|StallUntilLoaded|GetModel\(\)/,
  'The peer renderer must not use the detached animated-creature path that showed only eyes');
assert.match(applyRemoteAppearance, /IsBaked\(gRemoteCreationKey[\s\S]*BakeModel\(gRemoteCreationKey/,
  'An existing local creation must still finish its standalone skin bake');
assert.match(updateRemote, /visualKey = owner[\s\S]*\? localSpecies : gRemoteCreationKey/,
  'The invitation owner must provide the appearance in either profile');
assert.doesNotMatch(updateRemote, /strcmp\(CoopNet::GetRole\(\), "(?:host|guest)"\)/,
  'Visual ownership must not be inferred from profile number');
assert.doesNotMatch(updateRemote, /remote->mTransform\.SetScale\(pose\.scale\)/,
  'Native cell scale must not apply the rendered model scale a second time');
assert.match(updateRemote, /remote->mOpacity = pose\.visible \? 1\.0f : 0\.0f/,
  'The complete native peer cell must follow the published visibility');
assert.match(applyRemoteAppearance,
  /snapshot\.remoteAppearanceSequence <= gAppliedRemoteAppearanceSequence[\s\S]*RemoveRemoteCell\(/,
  'A changed complete appearance must rebuild the remote visual exactly once per sequence');
assert.match(updateRemote, /remotePositionReceivedTick[\s\S]*RemoveRemoteCell\(/,
  'A stale position stream must remove the visual after the peer leaves the world');
assert.match(updateRemote, /!snapshot\.inviteAccepted[\s\S]*RemoveRemoteCell\(/,
  'A TCP connection must not create a visual before the guest accepts the invitation');
assert.match(validateSavedWorld, /CoopSaves::Compatible\(source, target\)/,
  'Accepting an invitation must verify the complete launch-prepared save slot');
assert.doesNotMatch(validateSavedWorld, /CopyFile|copy_file|CopyDirectoryTree/,
  'The injected DLL must not rewrite a live SPORE save while either game is running');
assert.match(coopUpdate, /snapshot\.inviteFrom != CoopNet::GetRole\(\)[\s\S]*JoinSavedWorld\(\)/,
  'The invitation recipient must load the prepared world from either inviter role');
assert.match(updateRemote, /CoopVisual::SharedSize\(owner[\s\S]*player->mTargetSize = sharedSize\.target/,
  'The invited player must adopt the world owner size without changing owner growth');
assert.match(updateRemote, /player->mTransform\.SetScale/,
  'The local player scale must catch up when the peer grows first');
assert.doesNotMatch(probe, /g(?:RemoteCell|HostAppearanceProxy)Index\s*[<>]=?\s*0/,
  'Signed pool handles must never be rejected merely because their sign bit is set');
assert.match(applyRemoteAppearance,
  /CacheVisualModel\(snapshot\.remoteAppearanceBlob, sourceKey,[\s\S]*gRemoteCreationKey, gRemoteAppearanceResource/,
  'The remote visual must receive its own cached model identity');
assert.doesNotMatch(probe, /OpacityOverride|RemoveLocalAppearanceProxy|gLocalAppearanceProxy/,
  'The obsolete partial local-appearance replacement must stay removed');
assert.match(probe, /void UpdateHostAppearanceProxy\([\s\S]*CreatePlayerCellClone\(player,[\s\S]*&gRemoteCreationKey\)/,
  'The guest must render its own movement with a complete clone of the host creation');
assert.match(probe, /if \(proxyReady && !gLocalPlayerHiddenByProxy\)[\s\S]*gSavedLocalOpacity[\s\S]*if \(proxyReady && gLocalPlayerHiddenByProxy\)[\s\S]*player->mOpacity = 0\.0f/,
  'The guest native body must be hidden only after the complete host proxy is ready and remain hidden');
assert.match(coopUpdate, /gLocalPlayerHiddenByProxy \? 1\.0f : player->mOpacity, &renderPose/,
  'The hidden guest avatar must still publish a visible complete body to the host');
assert.match(probe, /void UpdateMirroredNpcs\([\s\S]*DeleteGuestLocalNpcs\(player\)[\s\S]*CreateCellFromResource/,
  'The joining window must replace local random NPCs with full authoritative owner cells');
assert.match(coopUpdate, /IsWorldOwner\(snapshot, CoopNet::GetRole\(\)\)[\s\S]*SubmitNpcSnapshot\(ReadAuthoritativeNpcs\(player\)\)/,
  'Only the world owner must publish the shared NPC population');
assert.match(updateStability, /gLastSubmittedAppearanceKey = ResourceKey\{\}/,
  'Growth must force the rebuilt local cell appearance to be resubmitted');
assert.match(updateStability, /RemoveRemoteCell\(/,
  'A local player-cell rebuild must remove the visual tied to the old render world');
assert.match(coopUpdate,
  /if \(!snapshot\.enabled \|\| !snapshot\.connected\)[\s\S]*RemoveRemoteCell\([\s\S]*return;/,
  'Disconnect handling must remove the frozen network clone before returning');
assert.match(coopUpdate, /connectionGeneration/,
  'A reconnect must reset per-connection appearance state so it is submitted again');
assert.match(stateHandler, /ObjectHasKey\(json, "players"[\s\S]*ClearRemotePeerStateLocked\(\)/,
  'A state snapshot without the peer must clear its position and appearance identity');
assert.match(net, /void ClearRemotePeerStateLocked\(\)[\s\S]*hasRemotePosition = false[\s\S]*remoteAppearanceSequence = 0/,
  'Peer-state cleanup must also reset sequence numbers for a replacement process');
assert.match(coopUpdate, /!snapshot\.hasRemotePosition[\s\S]*RemoveRemoteCell\("cooperative peer left"\)/,
  'A peer leaving the session must explicitly remove its frozen network clone');
for (const reset of [
  'gProgressSeedSent = false',
  'gProgressSync.Reset()',
  'gAppliedSpeciesSequence = 0',
  'gLastLocalSpecies.clear()'
]) {
  assert.ok(coopUpdate.includes(reset), `Reconnect handling must perform: ${reset}`);
}
assert.match(net, /type == "welcome"[\s\S]*speciesSequence = 0;[\s\S]*speciesBlob\.clear\(\)/,
  'A fresh server handshake must discard mirrored-editor sequence state from the old connection');
assert.match(probe, /UTFWin::IWindow\* FindVisiblePausePanel\(/,
  'The Esc panel must be detected from the visible UI hierarchy');
assert.match(probe, /IsReturnToGameCaption[\s\S]*\\u0412\\u0435\\u0440\\u043d\\u0443\\u0442\\u044c\\u0441\\u044f[\s\S]*Return to Game/,
  'The actual Russian and English Return-to-Game pause actions must anchor the invite menu item');
assert.match(inviteUI, /FindVisiblePausePanel\(/,
  'Invite placement must use the detected Esc panel when it is available');
assert.match(inviteUI, /const bool canOpenPicker = pauseMenuOpen && !editorMode[\s\S]*const bool showHostInvite = canOpenPicker && !gInvitePickerOpen/,
  'Either role\'s invite must be visible only in the Esc panel, never over gameplay');
assert.match(inviteUI, /PositionInviteButton\(gInviteButton\.get\(\), pausePanel\)/,
  'Invite placement must be anchored to the detected Esc panel');
assert.match(probe, /case kInvitePlayerButtonID:[\s\S]*CoopNet::SubmitInvite\(\)/,
  'The invite must be sent only after selecting a player in the picker');
assert.match(inviteUI, /DetectRussianGameUi\(root\)[\s\S]*CoopText\(u"Invite friend",/,
  'Custom invite text must follow the detected game-interface language');
assert.match(probe, /transparent caption colours[\s\S]*SetCaptionColor/,
  'Fresh buttons must set visible caption colours in every state');
assert.match(inviteUI, /TraceUiTreeChanges\(\)[\s\S]*CreateCoopButton\(/,
  'The host stage must capture native UI-tree changes before adding the cooperative menu item');
assert.match(probe, /SporeCoop\.UI\.ndjson/,
  'UI-tree diagnostics must be written to a dedicated diffable log');
assert.match(probe, /GetControlID\(\)[\s\S]*GetCommandID\(\)[\s\S]*GetComponentName\(\)/,
  'UI-tree diagnostics must preserve stable identity fields for closed/open comparison');
assert.doesNotMatch(inviteUI, /IsPaused\(\)|GetPauseCount\(/,
  'Invite visibility must not depend on simulation pause state');
assert.match(probe, /GetComponentName\(\)[\s\S]*"Button"/,
  'The Esc panel signature must use its visible button hierarchy instead of an unknown hard-coded ID');
assert.match(inviteUI, /remotePeerConnected[\s\S]*Waiting for friend to connect/,
  'The Esc invite must clearly remain disabled until the guest process connects');
assert.match(net, /remotePeerConnected = true/,
  'The network client must track whether the other role is connected');
assert.match(net, /gSnapshot\.inviteFrom = inviteFrom/,
  'The network client must preserve which window issued the invitation');
assert.match(probe, /UpdateAuthoritativePause[\s\S]*TimeManagerPause::Gameplay/,
  'The guest must use a separate gameplay pause that its own Esc toggle cannot release');
assert.match(net, /SubmitHostPause[\s\S]*hostPause/,
  'The network protocol must submit an authoritative host-pause state');
assert.match(submitAppearance, /gLastSubmittedAppearanceCellResource/,
  'A growth resource rebuild must resubmit appearance even when the species key is unchanged');
assert.match(probe, /gWasEditorMode[\s\S]*gLastSubmittedAppearanceKey = ResourceKey\{\}/,
  'Leaving the editor must force the edited appearance to be submitted to the world clone');
assert.match(sharedEditor,
  /gMirroredEditorRequestID != snapshot\.editorID[\s\S]*gMirroredEditorRequestTick >= 5000[\s\S]*OpenMirroredEditor/,
  'A slow editor transition must be debounced instead of reopened every frame');
for (const field of ['partCinematicPlayed', 'showMateButton', 'firstEditorEntry']) {
  assert.match(probe, new RegExp(`result\\.${field}`),
    `${field} must be read from the cell save state`);
  assert.match(probe, new RegExp(`value\\.${field}`),
    `${field} must be applied to the cell save state`);
  assert.match(net, new RegExp(`\\\\"${field}\\\\"`),
    `${field} must be sent over the cooperative protocol`);
}

console.log('PASS: native lifecycle source invariants. No gameplay was tested.');
