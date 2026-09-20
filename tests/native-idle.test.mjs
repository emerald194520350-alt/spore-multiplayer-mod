import assert from 'node:assert/strict';
import net from 'node:net';
import { spawn } from 'node:child_process';
import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const project = dirname(dirname(fileURLToPath(import.meta.url)));
const temp = await mkdtemp(join(tmpdir(), 'spore-native-idle-'));
const allocator = net.createServer();
await new Promise(resolve => allocator.listen(0, '127.0.0.1', resolve));
const port = allocator.address().port;
await new Promise(resolve => allocator.close(resolve));
const token = 'test-idle-host-012345678901234567890';
const server = spawn(process.env.SPORE_COOP_TEST_SERVER || join(project, 'SporeCoop.Server.exe'), [
  '--listen', '127.0.0.1', '--port', String(port), '--host-token', token,
  '--guest-token', 'test-idle-guest-012345678901234567890', '--save', join(temp, 'session.json')
], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
let client;
try {
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('Server startup timeout')), 5000);
    let output = '';
    server.once('error', reject);
    server.once('exit', code => reject(new Error('Server exited: ' + code)));
    server.stdout.on('data', chunk => {
      output += chunk;
      if (output.includes('LISTENING')) { clearTimeout(timer); resolve(); }
    });
  });
  client = spawn(join(project, 'bin/tests/NativeVisualTests.exe'), ['--idle'], {
    windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], env: {
      ...process.env, SPORE_COOP_ROLE: 'host', SPORE_COOP_SERVER: '127.0.0.1',
      SPORE_COOP_PORT: String(port), SPORE_COOP_TOKEN: token
    }
  });
  const result = await new Promise((resolve, reject) => {
    let output = '';
    const timer = setTimeout(() => { client.kill(); reject(new Error('Native idle test timeout')); }, 25000);
    client.once('error', reject);
    client.stdout.on('data', chunk => { output += chunk; });
    client.stderr.on('data', chunk => { output += chunk; });
    client.once('exit', code => { clearTimeout(timer); resolve({ code, output }); });
  });
  assert.equal(result.code, 0, result.output);
  process.stdout.write(result.output);
} finally {
  if (client && client.exitCode === null) client.kill();
  if (server.exitCode === null) {
    const exited = new Promise(resolve => server.once('exit', resolve));
    server.kill();
    await exited;
  }
}
