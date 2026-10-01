import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { EventEmitter } from 'node:events';

export function codexEnvironment(env = process.env) {
  const result = { ...env };
  for (const name of ['DISCORD_BOT_TOKEN', 'OPENAI_API_KEY', 'CODEX_API_KEY']) delete result[name];
  return result;
}

// Read/search tools only. Global Codex configuration is never modified.
const indexTools = {
  codebase_memory: ['list_projects', 'get_architecture', 'search_graph', 'query_graph', 'trace_path',
    'get_code_snippet', 'get_file_outline', 'search_code', 'check_index_coverage', 'index_status', 'detect_changes'],
  messcontrol_assets: ['asset_status', 'asset_search', 'asset_read', 'asset_references', 'asset_job_status'],
};

export function serverArgs(servers) {
  const args = ['app-server', '--listen', 'stdio://', '-c', 'forced_login_method="chatgpt"',
    '--disable', 'apps', '--disable', 'plugins', '--disable', 'browser_use',
    '--disable', 'computer_use', '--disable', 'hooks', '--disable', 'multi_agent'];
  for (const server of servers) {
    // Codex -c splits dotted paths itself; quoted TOML keys are not supported there.
    if (!/^[a-zA-Z0-9_-]+$/.test(server.name)) throw new Error('В имени MCP-сервера есть неподдерживаемые символы.');
    const name = server.name;
    const tools = indexTools[server.name];
    // CLI override tables must have a valid transport before config layers merge.
    // Preserve the transport kind: inherited HTTP URLs cannot accompany stdio commands.
    const table = tools ? {
      ...server.transport, type: undefined, enabled: true, enabled_tools: tools,
      startup_timeout_sec: server.startup_timeout_sec ?? 60,
      tool_timeout_sec: server.tool_timeout_sec ?? 120,
    } : server.transport?.type === 'streamable_http'
      ? { url: server.transport.url, enabled: false }
      : { command: 'codex', enabled: false };
    if (tools && server.transport?.type !== 'stdio') {
      throw new Error(`Индекс ${server.name} должен быть настроен как локальный MCP stdio.`);
    }
    args.push('-c', `mcp_servers.${name}=${tomlValue(table)}`);
  }
  return args;
}

function tomlValue(value) {
  if (typeof value === 'string' || typeof value === 'boolean' || typeof value === 'number') return JSON.stringify(value);
  if (Array.isArray(value)) return `[${value.map(tomlValue).join(',')}]`;
  if (value && typeof value === 'object') return `{${Object.entries(value).filter(([, item]) => item != null)
    .map(([key, item]) => `${JSON.stringify(key)}=${tomlValue(item)}`).join(',')}}`;
  throw new Error('Неподдерживаемое значение в конфигурации MCP.');
}

export class CodexRpc extends EventEmitter {
  constructor(executable, args, { cwd, spawnProcess = spawn, requestTimeoutMs = 30000 } = {}) {
    super();
    this.pending = new Map();
    this.nextId = 1;
    this.closed = false;
    this.requestTimeoutMs = requestTimeoutMs;
    this.proc = spawnProcess(executable, args, {
      cwd, env: codexEnvironment(), stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true, shell: false,
    });
    this.lines = createInterface({ input: this.proc.stdout });
    this.lines.on('line', line => {
      try { this.receive(JSON.parse(line)); }
      catch { this.fail(new Error('Codex вернул некорректный JSON.')); }
    });
    // Do not relay Codex stderr: MCP output can include private paths and content.
    this.proc.stderr.on('data', () => {});
    this.proc.stdin.on('error', error => this.fail(error));
    this.proc.on('error', error => this.fail(error));
    this.proc.on('exit', (code, signal) => this.fail(new Error(`Codex завершился (${code ?? signal}).`)));
  }

  send(value) {
    if (this.closed) throw new Error('Соединение с Codex закрыто.');
    this.proc.stdin.write(`${JSON.stringify(value)}\n`);
  }

  request(method, params = {}, timeoutMs = this.requestTimeoutMs) {
    return new Promise((resolve, reject) => {
      const id = this.nextId++;
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Codex: тайм-аут ${method}.`));
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer, method });
      try { this.send({ id, method, params }); }
      catch (error) { clearTimeout(timer); this.pending.delete(id); reject(error); }
    });
  }

  async initialize() {
    await this.request('initialize', {
      clientInfo: { name: 'messcontrol_discord', title: 'MessControl Discord', version: '0.1.0' },
    });
    this.send({ method: 'initialized', params: {} });
  }

  receive(message) {
    if (message.method && message.id !== undefined) {
      this.replyToServer(message);
    } else if (message.id !== undefined) {
      const pending = this.pending.get(message.id);
      if (!pending) return;
      clearTimeout(pending.timer);
      this.pending.delete(message.id);
      if (message.error) pending.reject(new Error(`Codex ${pending.method} (${message.error.code}): ${message.error.message}`));
      else pending.resolve(message.result);
    } else if (message.method) {
      this.emit('notification', message.method, message.params);
    }
  }

  replyToServer({ id, method, params }) {
    let result;
    switch (method) {
      case 'item/commandExecution/requestApproval':
      case 'item/fileChange/requestApproval': result = { decision: 'decline' }; break;
      case 'execCommandApproval':
      case 'applyPatchApproval': result = { decision: { denied: { rejection: 'Discord assistant is read-only.' } } }; break;
      case 'item/permissions/requestApproval': result = { permissions: {}, scope: 'turn' }; break;
      case 'mcpServer/elicitation/request': result = { action: 'decline' }; break;
      case 'item/tool/requestUserInput':
        result = { answers: Object.fromEntries((params.questions ?? []).map(question =>
          [question.id, { answers: ['Уточни этот вопрос у пользователя в итоговом ответе Discord.'] }])) };
        break;
      default:
        this.send({ id, error: { code: -32601, message: `Unsupported server request: ${method}` } });
        return;
    }
    this.send({ id, result });
    this.emit('blockedRequest', method);
  }

  fail(error) {
    if (this.closed) return;
    this.closed = true;
    for (const pending of this.pending.values()) { clearTimeout(pending.timer); pending.reject(error); }
    this.pending.clear();
    this.emit('disconnect', error);
    this.lines.close();
    this.proc.stdin.end();
    const timer = setTimeout(() => this.proc.kill(), 1500);
    timer.unref();
  }

  close() { this.fail(new Error('Соединение с Codex закрыто.')); }
}
