export const helpText = `**MessControl — помощник команды**
Упомяни меня и задай вопрос: «где находится инвентарь?», «сложно ли добавить…?», «как отключить…?».

**/mc ask** — вопрос, идея, сложность, отключение или удаление механики
**/mc find** — найти реализацию или настройку
**/mc task** — оформить обсуждение в черновик задачи и приложить Markdown
**/mc git** — status, log, updates; fetch и pull доступны операторам
**/mc reset** — начать новое обсуждение в этом канале
**/mc stop** — остановить текущий ответ в этом канале
**/mc help** — эта справка

Можно писать: @бот задача …, @бот найди …, @бот git status.
Я помню обращения ко мне в этом канале. Обычную переписку канала не собираю.
Задачи остаются черновиками; Git pull выполняется только как fast-forward при чистой рабочей папке.`;

const aliases = new Map([
  ['task', 'task'], ['задача', 'task'], ['find', 'find'], ['найди', 'find'],
  ['help', 'help'], ['помощь', 'help'], ['reset', 'reset'], ['stop', 'stop'],
]);
const gitActions = new Set(['status', 'log', 'updates', 'fetch', 'pull']);

export function parseMention(content, botId) {
  const pattern = new RegExp(`<@!?${botId}>`, 'g');
  if (!pattern.test(content)) return null;
  const text = content.replace(pattern, '').trim();
  if (!text) return { kind: 'help' };
  const [first, ...rest] = text.split(/\s+/);
  if (first.toLowerCase() === 'git') {
    const action = rest.join(' ').toLowerCase();
    if (!gitActions.has(action)) throw new Error('Для Git: status, log, updates, fetch или pull. Например: @бот git status');
    return { kind: 'git', action };
  }
  const kind = aliases.get(first.toLowerCase());
  if (kind && ['help', 'reset', 'stop'].includes(kind) && !rest.length) return { kind };
  if (kind && ['find', 'task'].includes(kind) && rest.length) return { kind, text: rest.join(' ') };
  return { kind: 'ask', text };
}

export function responsePayload(text, filename = 'response.md', alwaysAttach = false) {
  const content = String(text).trim() || 'Пустой ответ.';
  // UTF-16 Discord limit: avoid splitting a surrogate pair at the preview boundary.
  let preview = content.slice(0, 1700);
  if (/[\uD800-\uDBFF]$/.test(preview)) preview = preview.slice(0, -1);
  return {
    content: content.length > 1900 ? `${preview}\n\nПолный ответ — в приложенном файле.` : content,
    allowedMentions: { parse: [], repliedUser: false },
    files: alwaysAttach || content.length > 1900
      ? [{ attachment: Buffer.from(content, 'utf8'), name: filename }] : [],
  };
}

export function commandDefinition(SlashCommandBuilder) {
  return new SlashCommandBuilder().setName('mc').setDescription('Вопросы и задачи по MessControl')
    .addSubcommand(c => c.setName('ask').setDescription('Идея, сложность, отключение или другой вопрос')
      .addStringOption(o => o.setName('question').setDescription('Что обсудим?').setRequired(true).setMaxLength(6000)))
    .addSubcommand(c => c.setName('find').setDescription('Найти код, Blueprint или настройку')
      .addStringOption(o => o.setName('query').setDescription('Что найти?').setRequired(true).setMaxLength(6000)))
    .addSubcommand(c => c.setName('task').setDescription('Подготовить черновик задачи из обсуждения')
      .addStringOption(o => o.setName('description').setDescription('Что оформить в задачу?').setRequired(true).setMaxLength(6000)))
    .addSubcommand(c => c.setName('git').setDescription('Состояние Git и обновления')
      .addStringOption(o => o.setName('action').setDescription('Действие').setRequired(true)
        .addChoices(...[...gitActions].map(value => ({ name: value, value })))))
    .addSubcommand(c => c.setName('reset').setDescription('Сбросить контекст этого канала'))
    .addSubcommand(c => c.setName('stop').setDescription('Остановить ответ в этом канале'))
    .addSubcommand(c => c.setName('help').setDescription('Команды и примеры'));
}
