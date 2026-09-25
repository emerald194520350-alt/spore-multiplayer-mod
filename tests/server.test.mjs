// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import net from 'node:net';
import { spawn } from 'node:child_process';
import { mkdtemp, readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const project = dirname(dirname(fileURLToPath(import.meta.url)));
const temp = await mkdtemp(join(tmpdir(), 'spore-coop-test-'));
const save = join(temp, 'session.json');
const hostToken = 'test-host-012345678901234567890123456789';
const guestToken = 'test-guest-012345678901234567890123456789';
const fingerprint = 'a'.repeat(64);
const sockets = new Set();
let server, port, logs = '';
let checks = 0;
const check = (condition, message) => { assert.ok(condition, message); checks++; };

async function start() {
  const allocator = net.createServer();
  await new Promise(ok => allocator.listen(0, '127.0.0.1', ok));
  port = allocator.address().port;
  await new Promise(ok => allocator.close(ok));
  server = spawn(process.env.SPORE_COOP_TEST_SERVER || join(project, 'SporeCoop.Server.exe'), [
    '--listen', '127.0.0.1', '--port', String(port),
    '--host-token', hostToken, '--guest-token', guestToken, '--save', save
  ], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  await new Promise((ok, bad) => {
    const timer = setTimeout(() => bad(new Error('Server startup timed out\n' + logs)), 5000);
    server.once('error', bad);
    server.once('exit', code => { clearTimeout(timer); bad(new Error('Server exited ' + code + '\n' + logs)); });
    server.stdout.on('data', chunk => {
      logs += chunk.toString();
      if (logs.includes('LISTENING 127.0.0.1:' + port)) { clearTimeout(timer); ok(); }
    });
    server.stderr.on('data', chunk => { logs += chunk.toString(); });
  });
}

async function stop() {
  for (const socket of sockets) socket.destroy();
  sockets.clear();
  if (server && server.exitCode === null) {
    const exited = new Promise(ok => server.once('exit', ok));
    server.kill();
    await exited;
  }
  server = undefined;
  logs = '';
}

async function client() {
  const socket = net.createConnection({ host: '127.0.0.1', port });
  sockets.add(socket);
  await new Promise((ok, bad) => { socket.once('connect', ok); socket.once('error', bad); });
  let buffer = Buffer.alloc(0), messages = [], waiters = [], serial = 0, closed = false;
  const closedPromise = new Promise(ok => socket.on('close', () => {
    closed = true;
    for (const w of waiters) { clearTimeout(w.timer); w.bad(new Error('Client closed')); }
    waiters = [];
    ok();
  }));
  socket.on('error', () => {});
  socket.on('data', chunk => {
    buffer = Buffer.concat([buffer, chunk]);
    while (buffer.length >= 4) {
      const n = buffer.readInt32LE();
      assert.ok(n >= 2 && n <= 1048576);
      if (buffer.length < n + 4) break;
      const message = JSON.parse(buffer.subarray(4, n + 4));
      buffer = buffer.subarray(n + 4);
      const item = { message, serial: ++serial };
      const index = waiters.findIndex(w => item.serial > w.after && w.match(message));
      if (index >= 0) {
        const w = waiters.splice(index, 1)[0]; clearTimeout(w.timer); w.ok(message);
      } else messages.push(item);
    }
  });
  function wait(match, after = 0) {
    const index = messages.findLastIndex(m => m.serial > after && match(m.message));
    if (index >= 0) return Promise.resolve(messages.splice(index, 1)[0].message);
    if (closed) return Promise.reject(new Error('Client closed'));
    return new Promise((ok, bad) => {
      const w = { match, after, ok, bad };
      w.timer = setTimeout(() => {
        waiters = waiters.filter(item => item !== w);
        bad(new Error('Message wait timed out\n' + logs));
      }, 5000);
      waiters.push(w);
    });
  }
  function frame(data) {
    const body = Buffer.from(JSON.stringify(data));
    const head = Buffer.alloc(4); head.writeInt32LE(body.length);
    return Buffer.concat([head, body]);
  }
  return {
    socket, wait, closed: closedPromise, frame,
    send(data) { socket.write(frame(data)); },
    request(data, match) {
      const pending = wait(match, serial);
      socket.write(frame(data));
      return pending;
    }
  };
}

async function hello(role, token = role === 'host' ? hostToken : guestToken, fp = fingerprint) {
  const c = await client();
  const reply = await c.request({ type: 'hello', protocol: 7, role, token, fingerprint: fp },
    m => m.type === 'welcome' || m.type === 'error');
  return { c, reply };
}
const stateMessage = revision => m => m.type === 'state' && m.revision === revision;
const appearance = { modelInstance: 123, modelType: 0x2b978c46, modelGroup: 0,
  cellResource: 456, scale: 0.55, targetSize: 0.55, opacity: 1 };
const missions = () => Array(24).fill(0);
async function failure(c, data, contains) {
  const message = await c.request(data, m => m.type === 'error');
  check(message.message.includes(contains), 'Expected error ' + contains + ', got ' + message.message);
}

try {
  await start();
  const legacy = await client();
  const legacyReply = await legacy.request({ type: 'hello', protocol: 2, role: 'host', token: hostToken, fingerprint },
    m => m.type === 'error');
  check(legacyReply.type === 'error', 'Legacy DLLs cannot mix absolute progress with the new incremental protocol');
  await legacy.closed;
  const invalid = await hello('host', 'wrong-token-that-is-still-long-enough');
  check(invalid.reply.type === 'error', 'Invalid token must be refused');
  await invalid.c.closed;
  const { c: host, reply } = await hello('host');
  check(reply.type === 'welcome', 'Host handshake: ' + JSON.stringify(reply));
  const wrongBuild = await hello('guest', guestToken, 'b'.repeat(64));
  check(wrongBuild.reply.type === 'error', 'Different game/mod fingerprint must be refused');
  await wrongBuild.c.closed;
  const { c: guest } = await hello('guest');
  let state = await host.wait(m => m.type === 'state' && m.players.host && m.players.guest);
  check(state.dna === 0 && state.stage === 'cell', 'Initial shared state');
  await failure(guest, { type: 'award', revision: state.revision, eventId: 'fraud', amount: 100 }, 'Host authority');
  state = await host.request({ type: 'award', revision: state.revision, eventId: 'food-1', amount: 50 }, stateMessage(state.revision + 1));
  check(state.dna === 50, 'Shared DNA increased');
  const guestDna = await guest.wait(stateMessage(state.revision));
  check(guestDna.dna === 50, 'Guest receives identical DNA');
  await failure(host, { type: 'award', revision: state.revision, eventId: 'food-1', amount: 50 }, 'Duplicate');
  await failure(host, { type: 'award', revision: state.revision - 1, eventId: 'food-2', amount: 10 }, 'Stale');
  await failure(host, { type: 'award', revision: state.revision, eventId: 'negative', amount: -10 }, 'Invalid amount');
  const north = await host.request({ type: 'position', sequence: 0, position: [0, 10, 0], ...appearance }, m => m.type === 'position' && m.role === 'host');
  const west = await guest.request({ type: 'position', sequence: 0, position: [-10, 0, 0], ...appearance, modelInstance: 789 }, m => m.type === 'position' && m.role === 'guest');
  check(north.position[1] === 10 && west.position[0] === -10, 'Players move independently');
  check(west.modelInstance === 789 && west.scale === 0.55, 'Remote appearance is relayed with movement');
  const fullAppearance = Buffer.from('scp1-complete-cell-appearance').toString('base64');
  const relayedAppearance = await host.request({
    type: 'appearance', sequence: 0, modelInstance: 123,
    modelType: 0x2b978c46, modelGroup: 0, appearance: fullAppearance
  }, m => m.type === 'appearance' && m.role === 'host');
  const appearanceOnGuest = await guest.wait(m => m.type === 'appearance' && m.role === 'host');
  check(relayedAppearance.appearance === fullAppearance &&
    appearanceOnGuest.modelInstance === 123,
    'Complete creature appearance is relayed byte-for-byte');
  await failure(host, {
    type: 'appearance', sequence: 0, modelInstance: 123,
    modelType: 0x2b978c46, modelGroup: 0, appearance: fullAppearance
  }, 'Stale appearance');
  await failure(guest, { type: 'position', sequence: 0, position: [0, 0, 0], ...appearance }, 'Stale position');
  await failure(guest, { type: 'position', sequence: 1, position: ['bad', 0, 0], ...appearance }, 'Invalid position');
  await failure(guest, { type: 'position', sequence: 1, position: [88, 0, 0], ...appearance, opacity: 2 }, 'Invalid opacity');
  const unchangedPeer = await host.request({ type: 'snapshot' }, m => m.type === 'state');
  check(unchangedPeer.players.guest.position[0] === -10 && unchangedPeer.players.guest.sequence === 0,
    'A rejected position leaves both coordinates and sequence unchanged');
  await guest.request({ type: 'position', sequence: 1, position: [-12, 0, 0], ...appearance }, m => m.type === 'position' && m.role === 'guest');
  await failure(host, { type: 'appearance', sequence: 1, modelInstance: 123, modelType: 0x2b978c46, modelGroup: -1, appearance: fullAppearance }, 'Invalid modelGroup');
  await host.request({ type: 'appearance', sequence: 1, modelInstance: 123, modelType: 0x2b978c46, modelGroup: 0, appearance: fullAppearance }, m => m.type === 'appearance' && m.sequence === 1);
  check(true, 'Rejected appearance does not consume its sequence');
  await failure(guest, { type: 'editorOpen', editorBudget:16, editorId: 12345 }, 'Invitation must be accepted');

  state = await host.request({ type: 'invite' }, stateMessage(state.revision + 1));
  const invitation = await guest.wait(stateMessage(state.revision));
  check(state.invitePending && invitation.invitePending && !state.inviteAccepted,
    'Host invitation is shown to the guest');
  await failure(guest, { type: 'invite' }, 'already');
  state = await guest.request({ type: 'inviteResponse', accepted: true },
    stateMessage(state.revision + 1));
  const acceptedInvite = await host.wait(stateMessage(state.revision));
  check(state.inviteAccepted && acceptedInvite.inviteAccepted && !state.invitePending,
    'Accepted invitation unlocks automatic world joining');
  const historyBytes=Buffer.alloc(140);
  historyBytes.writeUInt32LE(0x31545348,0);historyBytes.writeUInt32LE(1,16);
  const historyPacket={type:'history',worldGeneration:state.worldGeneration,sequence:1,history:historyBytes.toString('base64')};
  const historyEcho=await host.request(historyPacket,m=>m.type==='history');
  const historyAtGuest=await guest.wait(m=>m.type==='history');
  check(historyEcho.history===historyPacket.history && historyAtGuest.history===historyPacket.history,
    'Host timeline is relayed identically to both players');
  await failure(guest,{...historyPacket,sequence:2},'World owner authority');
  await failure(host,historyPacket,'Stale history');
  await failure(host,{...historyPacket,sequence:2,worldGeneration:state.worldGeneration-1},'Stale world');
  await failure(host,{...historyPacket,sequence:2,history:'bad'},'Invalid history');
  await failure(host,{...historyPacket,sequence:2,history:historyBytes.subarray(0,24).toString('base64')},'Invalid history');
  const npcFrame = [
    0x80000001, 123456, 12, 34, 0, 0, 1, 0.55, 0.55, 1, 0, 0xfedcba98, 0x2b978c46, 0, 6, 0, 0, 0,
    0x80000002, 654321, 18, 35, 0, 0.7071067, 0.7071067, 0.8, 0.8, 1, 1, 789, 0x2b978c46, 0, 4, 0, 0, 0
  ];
  const relayedNpcs = await host.request({ type: 'npcSnapshot', actionAck: 0, sequence: 0, npcs: npcFrame },
    m => m.type === 'npcSnapshot' && m.role === 'host');
  const guestNpcs = await guest.wait(m => m.type === 'npcSnapshot' && m.role === 'host');
  check(relayedNpcs.npcs.length === 36 && guestNpcs.npcs[19] === 654321 &&
    guestNpcs.npcs[11] === 0xfedcba98,
    'World-owner NPC creatures are relayed as one bounded authoritative frame');
  await failure(guest, { type: 'npcSnapshot', actionAck: 0, sequence: 0, npcs: npcFrame },
    'World owner authority');
  await failure(host, { type: 'npcSnapshot', actionAck: 0, sequence: 1, npcs: [1, 2, 3] },
    'Invalid npcs');
  await failure(host, { type: 'npcSnapshot', actionAck: 0, sequence: 0, npcs: npcFrame }, 'Stale NPC');
  await failure(host, { type: 'npcSnapshot', actionAck: 0, sequence: 1, npcs: [...npcFrame.slice(0, 18), ...npcFrame.slice(0, 18)] }, 'Invalid npcs');
  const action = await guest.request({type:'worldAction',worldGeneration:state.worldGeneration,sequence:1,
    id:0x80000001,resource:123456,damage:2,removed:false,effects:false},m=>m.type==='worldAction');
  check(action.damage===2 && action.id===0x80000001,'Guest combat reaches the owner using the shared object ID');
  await host.wait(m=>m.type==='worldAction' && m.sequence===1);
  await failure(guest,{type:'worldAction',worldGeneration:state.worldGeneration,sequence:1,
    id:0x80000001,resource:123456,damage:2,removed:false,effects:false},'Stale world action');
  await failure(guest,{type:'worldAction',worldGeneration:state.worldGeneration-1,sequence:2,
    id:0x80000001,resource:123456,damage:0,removed:true,effects:false},'Stale world generation');
  const pickup=await guest.request({type:'worldAction',worldGeneration:state.worldGeneration,sequence:2,
    id:0x80000001,resource:123456,damage:0,removed:true,effects:true},m=>m.type==='worldAction'&&m.sequence===2);
  check(pickup.removed,'Guest removal requests shared loot generation on the owner');
  const duplicateRemoval=await guest.request({type:'worldAction',worldGeneration:state.worldGeneration,sequence:3,
    id:0x80000001,resource:123456,damage:0,removed:true,effects:true},m=>m.type==='worldAction'&&m.sequence===3);
  check(!duplicateRemoval.removed && !duplicateRemoval.effects,'An already removed shared object cannot generate owner loot twice');
  const food=[0x80000003,100,1,2,0,0,1,0.5,0.5,1,0,0,0,0,1,0,0,0];
  const full=Array.from({length:4094},(_,i)=>[i+10,...food.slice(1)]).flat();
  const fullFrame=await host.request({type:'npcSnapshot',sequence:1,actionAck:3,npcs:full},m=>m.type==='npcSnapshot'&&m.sequence===1);
  check(fullFrame.npcs.length===4094*18 && fullFrame.actionAck===3,'All loaded food/scenery objects fit in a full pool snapshot');
  await failure(host,{type:'npcSnapshot',sequence:2,actionAck:3,npcs:[...full,...food]},'Invalid npcs');
  const emptyNpcs = await host.request({ type: 'npcSnapshot', actionAck: 0, sequence: 2, npcs: [] }, m => m.type === 'npcSnapshot' && m.sequence === 2);
  check(emptyNpcs.npcs.length === 0, 'An empty authoritative population removes departed NPCs');
  state = await host.request({ type: 'hostPause', paused: true }, stateMessage(state.revision + 1));
  const pausedGuest = await guest.wait(stateMessage(state.revision));
  check(state.hostPaused && pausedGuest.hostPaused,
    'Only the host can set a shared gameplay pause');
  await failure(guest, { type: 'hostPause', paused: false }, 'World owner authority');
  const samePause = await host.request({ type: 'hostPause', paused: true }, m => m.type === 'state');
  check(samePause.revision === state.revision, 'Duplicate pause does not create a new revision');
  state = await host.request({ type: 'hostPause', paused: false }, stateMessage(state.revision + 1));
  const resumedGuest = await guest.wait(stateMessage(state.revision));
  check(!state.hostPaused && !resumedGuest.hostPaused,
    'Host resume releases the shared gameplay pause for both players');

  const seedUnlocks = Array(13).fill(0);
  seedUnlocks[2] = 1;
  await failure(host, { type: 'seedProgress', food: 999, plantFood: 'broken' }, 'Invalid plantFood');
  const rejectedSeed = await host.request({ type: 'snapshot' }, m => m.type === 'state');
  check(rejectedSeed.food === 0 && !rejectedSeed.progressInitialized && rejectedSeed.revision === state.revision,
    'Failed progress validation rolls back all partially assigned session fields');
  state = await host.request({
    type: 'seedProgress', food: 10, plantFood: 6, overPlantFood: 2,
    overAnimalFood: 1, spent: 3, unlocks: seedUnlocks, missions: missions(),
    killCount: 0, playerHasMoved: true, playerHasEaten: true,
    partCinematicPlayed: false, showMateButton: false, firstEditorEntry: false
  }, stateMessage(state.revision + 1));
  const seededGuest = await guest.wait(stateMessage(state.revision));
  check(state.progressInitialized && seededGuest.progressInitialized,
    'Host seeds the shared cell progression once');
  check(state.progress.food === 10 && state.progress.spent === 3 && state.progress.unlocks[2] === 1,
    'Seeded cell currency and unlocks are identical');

  const deltaUnlocks = Array(13).fill(0);
  deltaUnlocks[2] = 2;
  deltaUnlocks[7] = 1;
  state = await guest.request({
    type: 'progressDelta', sequence: 1, eventId: 'progress-1', food: 4, plantFood: 1,
    overPlantFood: 0, overAnimalFood: 3, spent: -1, unlocks: deltaUnlocks,
    missions: Object.assign(missions(), { 0: 1, 1: 2 }), killCount: 1,
    playerHasMoved: true, playerHasEaten: true,
    partCinematicPlayed: true, showMateButton: true, firstEditorEntry: true
  }, stateMessage(state.revision + 1));
  const progressedHost = await host.wait(stateMessage(state.revision));
  check(state.progress.food === 14 && state.progress.spent === 2 &&
    progressedHost.progress.food === 14 && progressedHost.progress.spent === 2,
    'Either player can add shared progress or refund shared points');
  check(state.progress.unlocks[2] === 2 && state.progress.unlocks[7] === 1,
    'Unlocked parts merge into one shared inventory');
  check(state.guestProgressSequence === 1 && state.hostProgressSequence === 0,
    'Progress snapshots acknowledge only the contributing player');
  check(state.progress.partCinematicPlayed && state.progress.showMateButton &&
    state.progress.firstEditorEntry,
    'Cell tutorial cinematics, mate prompt, and first editor entry are shared');
  await failure(guest, {
    type: 'progressDelta', sequence: 1, eventId: 'progress-1', food: 1, plantFood: 0,
    overPlantFood: 0, overAnimalFood: 0, spent: 0, unlocks: deltaUnlocks,
    missions: missions(), killCount: 0, playerHasMoved: false, playerHasEaten: false,
    partCinematicPlayed: false, showMateButton: false, firstEditorEntry: false
  }, 'Duplicate');

  state = await host.request({ type: 'editorOpen', editorBudget:16, editorId: 12345 }, stateMessage(state.revision + 1));
  const mirroredEditor = await guest.wait(stateMessage(state.revision));
  check(state.editorBudget===16 && mirroredEditor.editorBudget===16,
    'Both editors enter with the same native DNA budget');
  check(state.evolving && mirroredEditor.evolving && state.editorId === 12345,
    'Opening an editor mirrors its shared editor state');
  const sharedName=Buffer.from('Шип \"двойной\" 🦠','utf16le').toString('base64');
  const firstEditorSession=state.editorSession;
  const liveSpecies = Buffer.from('live-editor-model-v1').toString('base64');
  const liveOnGuest = await host.request({
    type: 'speciesLive', speciesName:sharedName, editorBudget:6, sequence: 0, baseSequence: state.speciesSequence, species: liveSpecies
  }, m => m.type === 'speciesLive' && m.role === 'host');
  const liveOnHost = await guest.wait(m => m.type === 'speciesLive' && m.role === 'host');
  check(liveOnGuest.editorBudget===6 && liveOnHost.editorBudget===6,
    'A ten-DNA purchase broadcasts model and remaining DNA in the same revision');
  check(liveOnGuest.species === liveSpecies && liveOnHost.species === liveSpecies,
    'Live creature edits are broadcast byte-for-byte');
  check(liveOnGuest.speciesName===sharedName && liveOnHost.speciesName===sharedName, 'Unicode species name shares the same revision as body and DNA');
  const staleEditor=await guest.request({type:'speciesLive',editorBudget:6,sequence:0,baseSequence:0,species:Buffer.from('stale').toString('base64')},m=>m.type==='speciesConflict');
  check(staleEditor.species===liveSpecies && staleEditor.clientSequence===0,'Stale editor updates return the current model instead of overwriting the peer');
  check(staleEditor.editorBudget===6,'A stale concurrent edit receives the authoritative budget for rebasing');
  check(staleEditor.speciesName===sharedName, 'A conflicting edit receives the authoritative name for rebasing');
  const nextEdit=Buffer.from('guest-added-mouth').toString('base64');
  const edited=await guest.request({type:'speciesLive',editorBudget:6,sequence:1,baseSequence:staleEditor.sequence,species:nextEdit},m=>m.type==='speciesLive'&&m.role==='guest');
  check(edited.editorBudget===6 && edited.species===nextEdit && edited.clientSequence===1,'Edits from player two are acknowledged and broadcast to player one');
  const refunded=await guest.request({type:'speciesLive',editorBudget:16,sequence:2,baseSequence:edited.sequence,species:liveSpecies},m=>m.type==='speciesLive'&&m.clientSequence===2);
  check(refunded.editorBudget===16 && refunded.species===liveSpecies,'Undo returns both the old model and its refunded budget');
  await failure(guest,{type:'speciesLive',editorBudget:-4,sequence:3,baseSequence:refunded.sequence,species:liveSpecies},'editorBudget');
  const finalLiveSpecies = Buffer.from('live-editor-model-v2').toString('base64');
  state = await guest.request({ type: 'editorClose', editorSession:state.editorSession, editorFinished:true, speciesName:Buffer.from('Шип \"двойной\" 🦠','utf16le').toString('base64'), editorBudget:6, species: finalLiveSpecies }, stateMessage(state.revision + 1));
  const editorClosedHost = await host.wait(stateMessage(state.revision));
  check(state.editorBudget===6 && editorClosedHost.editorBudget===6,'Final editor snapshot retains the submitted budget');
  check(!state.evolving && !editorClosedHost.evolving && state.species === finalLiveSpecies,
    'Either player can finish the mirrored editor with the same creature');

  check(state.editorFinished && editorClosedHost.editorFinished && state.speciesName===sharedName,
    'Guest acceptance delivers final name and completion together to both peers');
  const closedRevision=state.revision;
  host.send({type:'editorClose',editorSession:firstEditorSession,editorFinished:true,editorBudget:999,species:liveSpecies,speciesName:''});
  state=await host.request({type:'snapshot'},m=>m.type==='state' && m.revision>=closedRevision);
  check(state.revision===closedRevision && state.species===finalLiveSpecies && state.speciesName===sharedName,
    'Duplicate completion cannot replace the saved final creature');
  state = await host.request({ type: 'editorOpen', editorBudget:16, editorId: 54321 }, stateMessage(state.revision + 1));
  await guest.wait(stateMessage(state.revision));
  check(!state.editorFinished && state.editorSession>firstEditorSession, 'New editor visits clear the old completion');
  host.send({type:'editorClose',editorSession:firstEditorSession,editorFinished:true,editorBudget:6,species:finalLiveSpecies});
  const stillOpen=await host.request({type:'snapshot'},m=>m.type==='state' && m.editorSession===state.editorSession);
  check(stillOpen.evolving && stillOpen.species==='', 'Late completion from the previous visit cannot close a new editor');
  state = await guest.request({ type: 'editorClose', editorSession:state.editorSession, editorFinished:false, editorBudget:6, species: '' }, stateMessage(state.revision + 1));
  await host.wait(stateMessage(state.revision));
  check(!state.evolving && state.species === '',
    'An empty final editor snapshot unlocks the world without erasing the last valid creature');

  check(!state.editorFinished, 'Cancel never instructs the other game to accept its creature');

  state=await guest.request({type:'editorOpen',editorId:12345,editorBudget:6,species:finalLiveSpecies,speciesName:sharedName},stateMessage(state.revision+1));
  await host.wait(stateMessage(state.revision));
  await failure(guest,{type:'speciesLive',sequence:3,baseSequence:state.speciesSequence,editorBudget:6,species:finalLiveSpecies,speciesName:'AA=='},'speciesName');
  const renamed=await guest.request({type:'speciesLive',sequence:3,baseSequence:state.speciesSequence,editorBudget:6,species:finalLiveSpecies,speciesName:''},m=>m.type==='speciesLive' && m.clientSequence===3);
  const renamedOnHost=await host.wait(m=>m.type==='speciesLive' && m.clientSequence===3);
  check(renamed.speciesName==='' && renamedOnHost.speciesName==='' && renamed.editorBudget===6 && renamed.species===finalLiveSpecies,
    'Player two can clear the name without changing body or DNA');
  state=await host.request({type:'editorClose',editorSession:state.editorSession,editorFinished:true,editorBudget:6,species:finalLiveSpecies,speciesName:sharedName},stateMessage(state.revision+1));
  const acceptedOnGuest=await guest.wait(stateMessage(state.revision));
  check(acceptedOnGuest.editorFinished && acceptedOnGuest.speciesName===sharedName && acceptedOnGuest.species===finalLiveSpecies,
    'Host acceptance also delivers final name and creature to the guest');

  await guest.request({ type: 'requestEvolution' }, m => m.type === 'evolutionRequestAccepted');
  const requested = await host.wait(m => m.type === 'evolutionRequested');
  check(requested.role === 'guest', 'Guest can request evolution');
  state = await host.request({ type: 'beginEvolution', revision: state.revision, owner: 'guest' }, stateMessage(state.revision + 1));
  const guestEditor = await guest.wait(stateMessage(state.revision));
  check(state.evolving && guestEditor.evolving && state.editor === 'guest', 'Both enter shared editor state');
  await failure(host, { type: 'position', sequence: 1, position: [1, 1, 0], ...appearance }, 'World paused');
  await failure(host, { type: 'proposeEdit', revision: state.revision, eventId: 'locked', species: '' }, 'Editor is locked');
  await failure(guest, { type: 'proposeEdit', revision: state.revision, eventId: 'invalid', species: '!' }, 'Invalid base64');
  const species = Buffer.from('test-opaque-genome-v1').toString('base64');
  await guest.request({ type: 'proposeEdit', revision: state.revision, eventId: 'edit-1', species }, m => m.type === 'proposalAccepted');
  const proposal = await host.wait(m => m.type === 'editProposed');
  check(proposal.species === species, 'Editor proposal reaches host unchanged');
  await failure(host, { type: 'applyEdit', revision: state.revision, eventId: 'edit-1', cost: 100 }, 'Insufficient DNA');
  state = await host.request({ type: 'applyEdit', revision: state.revision, eventId: 'edit-1', cost: 20 }, stateMessage(state.revision + 1));
  const shared = await guest.wait(stateMessage(state.revision));
  check(state.dna === 30 && shared.dna === 30 && state.species === species && shared.species === species, 'Single DNA charge and identical genome');
  await failure(host, { type: 'applyEdit', revision: state.revision, eventId: 'edit-1', cost: 20 }, 'Duplicate');
  await failure(host, { type: 'finishEvolution', revision: state.revision, stage: 'creature' }, 'Both players');
  await guest.request({ type: 'ready', revision: state.revision }, m => m.type === 'state' && m.ready.includes('guest'));
  await host.request({ type: 'ready', revision: state.revision }, m => m.type === 'state' && m.ready.includes('host') && m.ready.includes('guest'));
  state = await host.request({ type: 'finishEvolution', revision: state.revision, stage: 'creature' }, stateMessage(state.revision + 1));
  const final = await guest.wait(stateMessage(state.revision));
  check(state.stage === 'creature' && final.stage === 'creature' && !state.evolving && !final.evolving, 'Both finish evolution');
  const saved = JSON.parse(await readFile(save, 'utf8'));
  check(saved.Dna === 30 && saved.Species === species && saved.Stage === 'creature', 'Persistent protocol state');
  check(saved.ProgressInitialized && saved.FoodProgression === 14 &&
    saved.EvolutionPointsSpent === 2 && saved.CellUnlocks[2] === 2 && saved.CellUnlocks[7] === 1,
    'Shared cell progress and unlocked parts are persisted');
  check(saved.CellMissions[0] === 1 && saved.CellMissions[1] === 2 && saved.PlayerHasEaten,
    'Tutorial mission state is persisted');
  check(saved.PartCinematicPlayed && saved.ShowMateButton && saved.FirstEditorEntry,
    'Tutorial transition flags are persisted');
  guest.socket.destroy();
  await guest.closed;
  const disconnected = await host.wait(m => m.type === 'state' && !m.players.guest);
  check(!disconnected.evolving, 'Disconnect releases editor');
  const { c: reconnect } = await hello('guest');
  const recovered = await reconnect.wait(m => m.type === 'state');
  check(recovered.species === species && recovered.dna === 30, 'Guest reconnect receives full shared state');
  const duplicate = await hello('guest');
  check(duplicate.reply.type === 'error', 'A third player cannot reuse the guest role');
  await duplicate.c.closed;
  const malformed = await client();
  const badLength = Buffer.alloc(4); badLength.writeInt32LE(1048577);
  malformed.socket.write(badLength);
  await malformed.closed;
  check(true, 'Oversized frame rejected');
  // A frame split across TCP packets must still be read exactly once.
  const ping = host.frame({ type: 'ping' });
  const pong = host.wait(m => m.type === 'pong');
  host.socket.write(ping.subarray(0, 2));
  host.socket.write(ping.subarray(2, 5));
  host.socket.write(ping.subarray(5));
  await pong;
  check(true, 'Fragmented TCP frame');
  const hostLeftReason = reconnect.wait(m => m.type === 'sessionEnded');
  host.socket.destroy();
  await host.closed;
  check((await hostLeftReason).reason === 'host_left', 'Host exit delivers its reason before closing the guest socket');
  await reconnect.closed;
  check(true, 'Host exit disconnects guest');
  await stop();
  await start();
  const { c: restored } = await hello('host');
  const loaded = await restored.wait(m => m.type === 'state');
  check(loaded.species === species && loaded.dna === 30 && loaded.stage === 'creature', 'Server restart restores protocol save');
  check(loaded.progressInitialized && loaded.progress.food === 14 &&
    loaded.progress.spent === 2 && loaded.progress.unlocks[7] === 1,
    'Server restart restores shared progress and parts');
  await failure(restored, { type: 'award', revision: loaded.revision, eventId: 'food-1', amount: 50 }, 'Duplicate');
  const { c: reverseGuest } = await hello('guest');
  await restored.wait(m => m.type === 'state' && m.players.host && m.players.guest);
  state = await reverseGuest.request({ type: 'invite' }, stateMessage(loaded.revision + 1));
  const reversePending = await restored.wait(stateMessage(state.revision));
  check(state.invitePending && reversePending.inviteFrom === 'guest' && !state.progressInitialized,
    'Either window can invite the other and selects its own saved world');
  state = await restored.request({ type: 'inviteResponse', accepted: true }, stateMessage(state.revision + 1));
  const reverseAccepted = await reverseGuest.wait(stateMessage(state.revision));
  check(state.inviteAccepted && state.inviteFrom === 'guest' && reverseAccepted.inviteFrom === 'guest',
    'The other window can accept a guest-originated invitation');
  state = await reverseGuest.request({ type: 'hostPause', paused: true }, stateMessage(state.revision + 1));
  check(state.hostPaused, 'Second-window world owner can pause');
  await failure(restored, { type: 'hostPause', paused: false }, 'World owner authority');
  state = await reverseGuest.request({ type: 'hostPause', paused: false }, stateMessage(state.revision + 1));
  check(!state.hostPaused, 'Second-window world owner can resume');
  await failure(restored, {
    type: 'seedProgress', food: 1, plantFood: 0, overPlantFood: 0, overAnimalFood: 0,
    spent: 0, unlocks: Array(13).fill(0), missions: missions(), killCount: 0,
    playerHasMoved: false, playerHasEaten: false,
    partCinematicPlayed: false, showMateButton: false, firstEditorEntry: false
  }, 'World owner authority');
  state = await reverseGuest.request({
    type: 'seedProgress', food: 9, plantFood: 3, overPlantFood: 0, overAnimalFood: 0,
    spent: 0, unlocks: Array(13).fill(0), missions: missions(), killCount: 0,
    playerHasMoved: true, playerHasEaten: true,
    partCinematicPlayed: false, showMateButton: false, firstEditorEntry: false
  }, stateMessage(state.revision + 1));
  check(state.progressInitialized && state.progress.food === 9,
    'The inviter, not the fixed host role, seeds the shared cell campaign');
  check(state.worldGeneration > invitation.worldGeneration &&
    state.hostProgressSequence === 0 && state.guestProgressSequence === 0,
    'A new world starts a fresh inventory acknowledgement generation');
  // Two independent pickups made from the same baseline must both survive.
  const connected = { host: restored, guest: reverseGuest };
  const gain = (eventId, slot) => ({ type: 'progressDelta', sequence: 1, eventId,
    food: 1, plantFood: 0, overPlantFood: 0, overAnimalFood: 0, spent: 0,
    unlocks: Array.from({ length: 13 }, (_, i) => i === slot ? 1 : 0),
    missions: missions(), killCount: 0, playerHasMoved: true, playerHasEaten: true,
    partCinematicPlayed: false, showMateButton: false, firstEditorEntry: false });
  if (!state.inviteAccepted) {
    state = await restored.request({ type: 'inviteResponse', accepted: true }, stateMessage(state.revision + 1));
  }
  const beforeConcurrent = state.revision;
  connected.host.send(gain('simultaneous-host', 3));
  connected.guest.send(gain('simultaneous-guest', 5));
  state = await connected.host.wait(stateMessage(beforeConcurrent + 2));
  check(state.food === 11 && state.unlocks[3] === 1 && state.unlocks[5] === 1,
    'Concurrent pickups from both players add food and preserve both unlocked parts');
  check(state.hostProgressSequence === 1 && state.guestProgressSequence === 1,
    'Each contributor receives a separate acknowledgement');
  await failure(connected.host, { ...gain('out-of-order', 8), sequence: 3 }, 'Out of order');
  const afterRejected = await connected.host.request({ type: 'snapshot' }, m => m.type === 'state');
  check(afterRejected.food === 11 && afterRejected.unlocks[8] === 0 && afterRejected.hostProgressSequence === 1,
    'Rejected progress changes neither inventory nor acknowledgements');
  console.log('PASS: ' + checks + ' protocol assertions. No gameplay was tested. Artifacts: ' + temp);
} catch (error) {
  console.error('Failed after ' + checks + ' assertions. Server log:\n' + logs);
  throw error;
} finally {
  await stop();
}
