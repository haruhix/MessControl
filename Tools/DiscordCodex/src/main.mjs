import { loadConfig, defaultConfigPath } from './config.mjs';
import { CodexService } from './service.mjs';
import { GitService } from './git.mjs';

const [command = 'run', ...args] = process.argv.slice(2);
const configIndex = args.indexOf('--config');
if (configIndex >= 0 && !args[configIndex + 1]) throw new Error('После --config нужен путь.');
const configPath = configIndex >= 0 ? args[configIndex + 1] : defaultConfigPath;
let service;
try {
  const config = await loadConfig(configPath, command === 'run');
  const executableIndex = args.indexOf('--codex-executable');
  if (executableIndex >= 0) {
    if (!args[executableIndex + 1]) throw new Error('После --codex-executable нужен путь.');
    config.codexExecutable = args[executableIndex + 1];
  }
  service = new CodexService(config);
  switch (command) {
    case 'run': {
      const { runBot } = await import('./bot.mjs');
      await runBot(config, service);
      break;
    }
    case 'doctor': {
      await service.initialize();
      const health = await service.health();
      console.log(JSON.stringify(health, null, 2));
      console.log(await new GitService(config.projectRoot).execute('status'));
      service.close();
      break;
    }
    case 'smoke': {
      await service.initialize();
      console.log('Проверяю один реальный запрос к Codex через вход ChatGPT…');
      // A separate ephemeral session keeps the smoke test out of Discord channel history.
      const { thread } = await service.rpc.request('thread/start', { ...service.threadParams(), ephemeral: true }, 60000);
      const { collectTurn } = await import('./service.mjs');
      const prompt = args.includes('--simple') ? 'Ответь одним словом: подключено. Не вызывай инструменты.'
        : 'Проверь codebase_memory через list_projects и messcontrol_assets через asset_status. Затем выполни git status --short --branch. Кратко сообщи, доступны ли индексы и есть ли конфликт Git. Ничего не изменяй.';
      console.log(await collectTurn(service.rpc, thread.id, prompt, config.turnTimeoutMs));
      service.close();
      break;
    }
    default: throw new Error('Команды: run, doctor, smoke [--simple] [--config путь].');
  }
} catch (error) {
  service?.close();
  console.error(error.message);
  process.exitCode = 1;
}
