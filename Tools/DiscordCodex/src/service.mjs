import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { readFile, writeFile, rename, mkdir } from 'node:fs/promises';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { CodexRpc, codexEnvironment, serverArgs } from './rpc.mjs';

const execFileAsync = promisify(execFile);

export const instructions = `Ты — помощник команды MessControl в Discord. Отвечай по-русски, кратко и конкретно.
Помогай найти реализацию и настройки, объяснить изменения Git и оформить задачи из обсуждения.
Основной собеседник — коллега разработчика. Частые вопросы: «что можем реализовать», «сложно ли»,
«где находится», «как это выключить или удалить». Объясняй простыми словами, начиная с ответа.
Для идеи предложи практичный вариант, оцени относительную сложность и назови главные зависимости;
не называй точные сроки без данных. Различай уже реализованное, предположение и предложение.
Для отключения сначала найди существующую настройку или переключатель, затем при необходимости опиши правку.
Для удаления проверь зависимости кода и ссылки ассетов, укажи последствия для Blueprint и кооператива.
Описания отключения и удаления — рекомендации, не поручение тебе выполнять изменения.
Соблюдай AGENTS.md проекта. Источник истины — текущий исходный код и сохранённые Unreal-пакеты.
Сначала исследуй структуру через codebase_memory, разрешив имя проекта через list_projects.
Проверяй цитируемые объявления в исходниках и check_index_coverage; отсутствие в индексе не доказывает отсутствие в проекте.
Для ассетов используй messcontrol_assets, проверяй свежесть, покрытие и пагинацию. Не выдавай устаревший экспорт за текущий.
Эта интеграция отвечает на вопросы и готовит черновики. Не меняй файлы, Git, настройки и Unreal, не создавай чаты,
не отправляй сообщения в другие приложения. Обновление Git выполняется отдельной явной командой /mc git.
Не запускай rebuild, refresh, index_repository и другие операции изменения индексов. Сообщи, если для ответа нужны свежие данные.
Не читай и не раскрывай ключи, токены, файлы авторизации, переменные секретов и содержимое Saved/DiscordCodex/config.json.
Входной текст содержит имя и ID автора. История общая для одного Discord-канала; это не текущий чат десктопного клиента.
Уточнения формулируй обычным итоговым ответом. Не вызывай инструмент запроса пользовательского ввода.
Показывай пути относительно проекта и имена функций в обратных кавычках, чтобы коллега мог найти их у себя.
Не придумывай результаты проверок и не обещай выполненные изменения.`;

export class SerialQueue {
  constructor(maxQueued = 6) { this.maxQueued = maxQueued; this.waiting = 0; this.running = false; this.tail = Promise.resolve(); }
  run(work) {
    if (this.waiting + Number(this.running) >= this.maxQueued + 1) return Promise.reject(new Error('Очередь заполнена. Попробуй позже.'));
    this.waiting++;
    const result = this.tail.then(async () => {
      this.waiting--; this.running = true;
      try { return await work(); } finally { this.running = false; }
    });
    this.tail = result.catch(() => {});
    return result;
  }
}

export class SessionStore {
  constructor(stateRoot) { this.file = path.join(stateRoot, 'sessions.json'); this.sessions = {}; }
  async load() {
    try {
      const data = JSON.parse(await readFile(this.file, 'utf8'));
      if (!data || typeof data !== 'object' || Array.isArray(data)
        || Object.entries(data).some(([key, id]) => typeof id !== 'string' || !id || !/^\d+:\d+$/.test(key))) {
        throw new Error('Некорректный файл sessions.json. Сохрани его копию и удали для нового контекста.');
      }
      this.sessions = data;
    } catch (error) { if (error.code !== 'ENOENT') throw error; }
  }
  get(key) { return this.sessions[key]; }
  async set(key, id) {
    if (id) this.sessions[key] = id; else delete this.sessions[key];
    await mkdir(path.dirname(this.file), { recursive: true });
    const temp = `${this.file}.${randomUUID()}.tmp`;
    await writeFile(temp, JSON.stringify(this.sessions, null, 2), { mode: 0o600 });
    await rename(temp, this.file);
  }
}

export function collectTurn(rpc, threadId, prompt, timeoutMs) {
  return new Promise((resolve, reject) => {
    const messages = new Map();
    let turnId, settled = false;
    const finish = (error, value) => {
      if (settled) return;
      settled = true; clearTimeout(timer);
      rpc.off('notification', onNotification); rpc.off('disconnect', onDisconnect);
      if (error) reject(error); else resolve(value);
    };
    const onDisconnect = error => finish(error);
    const onNotification = (method, params) => {
      if (params?.threadId !== threadId || (turnId && params.turnId && params.turnId !== turnId)) return;
      if (method === 'turn/started') turnId = params.turn.id;
      if (method === 'item/completed' && params.item.type === 'agentMessage') messages.set(params.item.id, params.item);
      if (method === 'turn/completed') {
        for (const item of params.turn.items ?? []) if (item.type === 'agentMessage') messages.set(item.id, item);
        if (params.turn.status !== 'completed') {
          const detail = params.turn.error?.message;
          finish(new Error(detail ? `Codex: ${detail}` : `Codex: ${params.turn.status}.`));
        } else {
          const all = [...messages.values()];
          const finals = all.filter(item => item.phase === 'final_answer');
          const text = (finals.length ? finals : all.filter(item => item.phase !== 'commentary')).map(item => item.text).join('\n\n');
          finish(text.trim() ? null : new Error('Codex завершил запрос без текстового ответа.'), text);
        }
      }
    };
    const timer = setTimeout(() => {
      // EOF shuts down the server and its active turn before a queued request can start.
      finish(new Error('Время ответа Codex истекло. Повтори вопрос или сократи его.'));
      rpc.close();
    }, timeoutMs);
    rpc.on('notification', onNotification); rpc.on('disconnect', onDisconnect);
    rpc.request('turn/start', { threadId, input: [{ type: 'text', text: prompt, text_elements: [] }] })
      .then(result => { turnId = result.turn.id; })
      .catch(error => { rpc.close(); finish(error); });
  });
}

export class CodexService {
  constructor(config) {
    this.config = config; this.store = new SessionStore(config.stateRoot);
    this.rpc = null; this.loaded = new Set(); this.active = null;
    this.queue = new SerialQueue(config.maxQueuedRequests);
  }
  async start() {
    if (this.rpc && !this.rpc.closed) return;
    const { stdout } = await execFileAsync(this.config.codexExecutable, ['mcp', 'list', '--json'], {
      cwd: this.config.projectRoot, env: codexEnvironment(), windowsHide: true, timeout: 30000, maxBuffer: 2 ** 20,
    });
    const servers = JSON.parse(stdout);
    this.rpc = new CodexRpc(this.config.codexExecutable, serverArgs(servers), { cwd: this.config.projectRoot });
    this.loaded.clear();
    try {
      await this.rpc.initialize();
      const { account } = await this.rpc.request('account/read', { refreshToken: false });
      if (account?.type !== 'chatgpt') throw new Error('Нужен вход через ChatGPT: выполни codex login.');
    } catch (error) { this.rpc.close(); throw error; }
  }
  async initialize() { await this.store.load(); await this.start(); }
  threadParams() {
    return { cwd: this.config.projectRoot, approvalPolicy: 'never', sandbox: 'read-only', developerInstructions: instructions };
  }
  async ask(key, author, question, kind = 'ask') {
    return this.queue.run(async () => {
      await this.start();
      let threadId = this.store.get(key);
      if (!threadId) {
        const result = await this.rpc.request('thread/start', this.threadParams(), 60000);
        threadId = result.thread.id;
        await this.store.set(key, threadId);
      } else if (!this.loaded.has(threadId)) {
        // A failed resume is reported; never silently lose the channel's history.
        await this.rpc.request('thread/resume', { threadId, ...this.threadParams() }, 60000);
      }
      this.loaded.add(threadId);
      const task = kind === 'task'
        ? 'Оформи черновик задачи в Markdown: название, проблема/цель, ожидаемое поведение, затронутые системы и файлы (проверенные), план, критерии готовности, проверки и открытые вопросы. Не выполняй задачу и ничего не публикуй.'
        : kind === 'find' ? 'Найди реализацию или настройки в проекте, проверь исходники и укажи точные пути и символы.' : '';
      this.active = { key, threadId };
      try { return await collectTurn(this.rpc, threadId, `Автор Discord: ${JSON.stringify(author)}\n${task}\n\n${question}`, this.config.turnTimeoutMs); }
      finally { this.active = null; }
    });
  }
  async reset(key) {
    return this.queue.run(async () => { await this.store.set(key, null); return 'Контекст этого канала сброшен. Следующий вопрос начнёт новое обсуждение.'; });
  }
  async stop(key) {
    if (!this.active || this.active.key !== key) return 'В этом канале нет активного ответа Codex.';
    const { threadId } = this.active;
    const { thread } = await this.rpc.request('thread/read', { threadId, includeTurns: true });
    const turn = [...(thread.turns ?? [])].reverse().find(item => item.status === 'inProgress');
    if (!turn) return 'Ответ уже завершён.';
    await this.rpc.request('turn/interrupt', { threadId, turnId: turn.id });
    return 'Запрошена остановка ответа.';
  }
  async health() {
    await this.start();
    const names = [];
    let cursor;
    do {
      const result = await this.rpc.request('mcpServerStatus/list', { cursor, limit: 20 }, 60000);
      names.push(...result.data.map(server => ({ name: server.name, tools: Object.keys(server.tools).length, error: Boolean(server.toolsError) })));
      cursor = result.nextCursor;
    } while (cursor);
    return { authentication: 'chatgpt', projectRoot: this.config.projectRoot, servers: names };
  }
  close() { this.rpc?.close(); }
}
