import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { existsSync } from 'node:fs';

const execFileAsync = promisify(execFile);
export class GitService {
  constructor(projectRoot, run = null) {
    this.projectRoot = projectRoot;
    this.run = run ?? (async args => {
      try {
        const { stdout, stderr } = await execFileAsync('git', args, {
          cwd: projectRoot, windowsHide: true, shell: false, timeout: 90000, maxBuffer: 2 ** 21,
          env: { ...process.env, GIT_TERMINAL_PROMPT: '0', GCM_INTERACTIVE: 'Never' },
        });
        return (stdout || stderr).trimEnd();
      } catch (error) {
        throw new Error(`Git: ${(error.stderr || error.stdout || error.message).trim().slice(0, 2500)}`);
      }
    });
  }
  async checkPull() {
    const dirty = await this.run(['status', '--porcelain=v1']);
    if (dirty.trim()) throw new Error('Pull остановлен: есть локальные изменения или конфликты. Сначала заверши слияние и сохрани работу.');
    for (const marker of ['MERGE_HEAD', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'rebase-merge', 'rebase-apply', 'BISECT_LOG']) {
      const absolute = await this.run(['rev-parse', '--path-format=absolute', '--git-path', marker]);
      if (existsSync(absolute)) throw new Error('Pull остановлен: в репозитории не завершена операция Git.');
    }
    await this.run(['rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{upstream}']);
  }
  async execute(action) {
    switch (action) {
      case 'status': return this.run(['status', '--short', '--branch']);
      case 'log': return this.run(['log', '-12', '--date=short', '--format=%h %ad %an: %s']);
      case 'updates': {
        const upstream = await this.run(['rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{upstream}']);
        const counts = await this.run(['rev-list', '--left-right', '--count', 'HEAD...@{upstream}']);
        const commits = await this.run(['log', '--max-count=20', '--format=%h %an: %s', 'HEAD..@{upstream}']);
        return `Ветка сравнения: ${upstream}\nЛокальные / входящие коммиты: ${counts}\n${commits || 'Входящих коммитов нет.'}\nДанные последнего fetch. Для обновления: /mc git действие:fetch`;
      }
      case 'fetch': {
        const result = await this.run(['fetch', '--prune']);
        const summary = await this.execute('updates').catch(() => 'Upstream не настроен.');
        return `Fetch выполнен.\n${result}\n${summary}`;
      }
      case 'pull':
        await this.checkPull();
        return this.run(['pull', '--ff-only', '--no-rebase']);
      default: throw new Error('Неизвестное действие Git.');
    }
  }
}
