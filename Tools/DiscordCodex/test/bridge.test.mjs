import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { PassThrough, Writable } from 'node:stream';
import { mkdtemp, mkdir, rm, writeFile, readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { defaultProjectRoot, validateConfig, authorize, authorizeGitUpdate } from '../src/config.mjs';
import { CodexRpc, codexEnvironment, serverArgs } from '../src/rpc.mjs';
import { collectTurn, SerialQueue, SessionStore } from '../src/service.mjs';
import { parseMention, responsePayload, commandDefinition } from '../src/commands.mjs';
import { GitService } from '../src/git.mjs';
import { dispatch, taskDate } from '../src/bot.mjs';
import { SlashCommandBuilder } from 'discord.js';

const guildId = '111111111111111111', userId = '222222222222222222', colleagueId = '333333333333333333';
const channelId = '444444444444444444', botId = '555555555555555555';
const config = () => validateConfig({ projectRoot: defaultProjectRoot,
  allowedGuildIds: [guildId], allowedUserIds: [userId, colleagueId], operatorUserIds: [userId],
  allowedChannelIds: [channelId], allowGitUpdates: true });
const context = { guildId, userId, channelId };

async function tempDirectory(t) {
  const root = path.resolve(defaultProjectRoot, 'Saved/DiscordCodex/tests');
  await mkdir(root, { recursive: true });
  const dir = await mkdtemp(path.join(root, 'case-'));
  t.after(async () => {
    const relative = path.relative(root, path.resolve(dir));
    assert.ok(relative && !relative.startsWith('..') && !path.isAbsolute(relative));
    await rm(dir, { recursive: true, force: true });
  });
  return dir;
}

function fakeRpc(t, respond = () => {}) {
  const proc = new EventEmitter();
  proc.stdout = new PassThrough(); proc.stderr = new PassThrough();
  const sent = [];
  proc.stdin = new Writable({ write(chunk, encoding, callback) {
    const message = JSON.parse(chunk.toString()); sent.push(message);
    respond(message, proc); callback();
  } });
  proc.kill = () => { proc.emit('exit', 0); };
  const rpc = new CodexRpc('fake', [], { spawnProcess: () => proc, requestTimeoutMs: 100 });
  t.after(() => { rpc.close(); proc.stdout.end(); proc.stderr.end(); });
  return { rpc, proc, sent };
}
const send = (proc, message) => proc.stdout.write(`${JSON.stringify(message)}\n`);

test('access requires allowed guild, human user and channel; threads inherit channel access', () => {
  const c = config();
  assert.equal(authorize(c, context), true);
  assert.equal(authorize(c, { ...context, userId: colleagueId }), true);
  for (const override of [{ guildId: null }, { userId: botId }, { channelId: botId }, { bot: true }]) {
    assert.equal(authorize(c, { ...context, ...override }), false);
  }
  assert.equal(authorize(c, { ...context, channelId: botId, parentId: channelId }), true);
  assert.equal(authorizeGitUpdate(c, colleagueId), false);
  assert.equal(authorizeGitUpdate(c, userId), true);
  assert.throws(() => validateConfig({ ...c, allowedUserIds: [] }), /allowedUserIds/);
});

test('mention questions support both Discord tag formats and never interpret arbitrary Git arguments', () => {
  assert.deepEqual(parseMention(`<@${botId}> где это выключить?`, botId), { kind: 'ask', text: 'где это выключить?' });
  assert.deepEqual(parseMention(`<@!${botId}> задача добавим эффекты еды`, botId), { kind: 'task', text: 'добавим эффекты еды' });
  assert.equal(parseMention('обычный разговор', botId), null);
  assert.throws(() => parseMention(`<@${botId}> git pull; del *`, botId), /Для Git/);
});

test('all slash commands serialize to a valid definition', () => {
  const definition = commandDefinition(SlashCommandBuilder).toJSON();
  assert.equal(definition.name, 'mc');
  assert.deepEqual(definition.options.map(item => item.name), ['ask', 'find', 'task', 'git', 'reset', 'stop', 'help']);
});

test('long answers and task drafts preserve complete Unicode content in attachments and disable mentions', () => {
  const text = '🦷'.repeat(1100) + '\n@everyone';
  const result = responsePayload(text);
  assert.ok(result.content.length <= 2000);
  assert.equal(result.files[0].attachment.toString(), text);
  assert.deepEqual(result.allowedMentions.parse, []);
  assert.equal(responsePayload('Задача', 'task.md', true).files[0].name, 'task.md');
});

test('Codex environment does not inherit Discord or API keys', () => {
  const env = codexEnvironment({ DISCORD_BOT_TOKEN: 'discord', OPENAI_API_KEY: 'openai', CODEX_API_KEY: 'codex', PATH: 'path', CODEX_HOME: 'home' });
  assert.deepEqual(env, { PATH: 'path', CODEX_HOME: 'home' });
});

test('MCP overrides preserve transports, limit tools and disable connectors', () => {
  const args = serverArgs([
    { name: 'codebase_memory', transport: { type: 'stdio', command: 'python', args: ['some path\\server.py'], env: null } },
    { name: 'unreal_epic', transport: { type: 'streamable_http', url: 'http://localhost:1000' } },
    { name: 'blender', transport: { type: 'stdio', command: 'blender.exe' } },
  ]);
  const memory = args.find(arg => arg.startsWith('mcp_servers.codebase_memory='));
  assert.ok(memory.includes('"command"="python"'));
  assert.ok(memory.includes('"enabled_tools"='));
  assert.ok(!memory.includes('index_repository'));
  const unreal = args.find(arg => arg.startsWith('mcp_servers.unreal_epic='));
  assert.ok(unreal.includes('"url"=') && !unreal.includes('"command"='));
  assert.ok(args.includes('apps') && args.includes('plugins'));
  assert.throws(() => serverArgs([{ name: 'bad.key' }]), /имени MCP/);
});

test('RPC matches out-of-order responses and rejects on disconnect', async t => {
  const { rpc, proc, sent } = fakeRpc(t);
  const first = rpc.request('one'), second = rpc.request('two');
  send(proc, { id: sent[1].id, result: 'two' });
  send(proc, { id: sent[0].id, result: 'one' });
  assert.deepEqual(await Promise.all([first, second]), ['one', 'two']);
  const pending = rpc.request('three');
  proc.emit('exit', 1);
  await assert.rejects(pending, /завершился/);
});

test('approval requests are declined with the installed protocol format', async t => {
  const { rpc, sent } = fakeRpc(t);
  rpc.receive({ id: 'server-1', method: 'item/commandExecution/requestApproval', params: {} });
  rpc.receive({ id: 'server-2', method: 'execCommandApproval', params: {} });
  assert.deepEqual(sent[0], { id: 'server-1', result: { decision: 'decline' } });
  assert.equal(sent[1].result.decision.denied.rejection, 'Discord assistant is read-only.');
});

test('turn collector ignores other threads and commentary, even when completion precedes start response', async t => {
  const { rpc, proc } = fakeRpc(t, (message, proc) => {
    if (message.method !== 'turn/start') return;
    send(proc, { method: 'item/completed', params: { threadId: 'other', item: { type: 'agentMessage', id: 'x', text: 'wrong' } } });
    send(proc, { method: 'item/completed', params: { threadId: 'thread', item: { type: 'agentMessage', id: 'c', text: 'progress', phase: 'commentary' } } });
    send(proc, { method: 'item/completed', params: { threadId: 'thread', item: { type: 'agentMessage', id: 'a', text: 'answer', phase: 'final_answer' } } });
    send(proc, { method: 'turn/completed', params: { threadId: 'thread', turn: { id: 'turn', status: 'completed', items: [] } } });
    send(proc, { id: message.id, result: { turn: { id: 'turn' } } });
  });
  assert.equal(await collectTurn(rpc, 'thread', 'question', 1000), 'answer');
});

test('failed turns do not masquerade as successful answers', async t => {
  const { rpc } = fakeRpc(t, (message, proc) => {
    send(proc, { id: message.id, result: { turn: { id: 'turn' } } });
    send(proc, { method: 'turn/completed', params: { threadId: 'thread', turn: { status: 'failed', error: { message: 'test failure' } } } });
  });
  await assert.rejects(collectTurn(rpc, 'thread', 'question', 1000), /test failure/);
});

test('turn deadline closes the process and removes listeners', async t => {
  const { rpc } = fakeRpc(t, (message, proc) => send(proc, { id: message.id, result: { turn: { id: 'turn' } } }));
  await assert.rejects(collectTurn(rpc, 'thread', 'question', 15), /Время ответа/);
  assert.equal(rpc.closed, true);
  assert.equal(rpc.listenerCount('notification'), 0);
});

test('queue is bounded before the first microtask and recovers after failure', async () => {
  const queue = new SerialQueue(1);
  const order = [];
  const first = queue.run(async () => { order.push(1); throw new Error('first failed'); });
  const second = queue.run(async () => { order.push(2); return 2; });
  await assert.rejects(queue.run(async () => order.push(3)), /Очередь/);
  await assert.rejects(first, /first failed/);
  assert.equal(await second, 2);
  assert.deepEqual(order, [1, 2]);
});

test('channel context survives restart and reset', async t => {
  const dir = await tempDirectory(t);
  const first = new SessionStore(dir); await first.load();
  await first.set(`${guildId}:${channelId}`, 'thread-id');
  const second = new SessionStore(dir); await second.load();
  assert.equal(second.get(`${guildId}:${channelId}`), 'thread-id');
  await second.set(`${guildId}:${channelId}`, null);
  assert.deepEqual(JSON.parse(await readFile(second.file, 'utf8')), {});
});

test('Git pull refuses dirty work before any network/mutation command', async () => {
  const calls = [];
  const git = new GitService(defaultProjectRoot, async args => { calls.push(args); return 'UU Content/Maps/L_Mouth.umap'; });
  await assert.rejects(git.execute('pull'), /локальные изменения/);
  assert.deepEqual(calls, [['status', '--porcelain=v1']]);
});

test('Git status/updates use fixed arguments; fetch does not merge', async () => {
  const calls = [];
  const git = new GitService(defaultProjectRoot, async args => { calls.push(args); return ''; });
  await git.execute('fetch');
  assert.deepEqual(calls[0], ['fetch', '--prune']);
  assert.ok(!calls.some(args => args.includes('merge') || args.includes('pull')));
  await assert.rejects(git.execute('status && echo hacked'), /Неизвестное/);
});

test('real repository: dirty checkout and unfinished merge both block pull', async t => {
  const dir = await tempDirectory(t);
  const git = (...args) => execFileSync('git', args, { cwd: dir, windowsHide: true, encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] });
  git('init', '-b', 'main'); git('config', 'user.email', 'test@localhost'); git('config', 'user.name', 'Test');
  await writeFile(path.join(dir, 'file.txt'), 'one'); git('add', 'file.txt'); git('commit', '-m', 'initial');
  const service = new GitService(dir);
  await writeFile(path.join(dir, 'file.txt'), 'two');
  await assert.rejects(service.execute('pull'), /локальные изменения/);
  await writeFile(path.join(dir, 'file.txt'), 'one');
  await writeFile(path.join(dir, '.git/MERGE_HEAD'), git('rev-parse', 'HEAD'));
  await assert.rejects(service.execute('pull'), /не завершена операция/);
});

test('dispatch prevents colleague Git writes and saves task drafts outside source', async t => {
  const c = { ...config(), stateRoot: await tempDirectory(t) };
  let called = false;
  const service = { queue: new SerialQueue(), ask: async () => '# Задача\nКритерии готовности.' };
  const git = { execute: async () => { called = true; } };
  await assert.rejects(dispatch(c, service, git, { ...context, userId: colleagueId }, { kind: 'git', action: 'pull' }), /операторам|operatorUserIds/);
  assert.equal(called, false);
  const result = await dispatch(c, service, git, context, { kind: 'task', text: 'Оформи обсуждение' });
  assert.equal(result.attach, true);
  assert.equal(await readFile(path.join(c.stateRoot, 'tasks', result.filename), 'utf8'), result.text);
});

test('task date follows Yakutsk time across the UTC date boundary', () => {
  assert.equal(taskDate('Asia/Yakutsk', new Date('2026-09-30T18:00:00Z')), '2026-10-01');
});
