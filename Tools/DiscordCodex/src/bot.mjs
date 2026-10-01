import { Client, GatewayIntentBits, Events, SlashCommandBuilder, MessageFlags } from 'discord.js';
import { mkdir, writeFile, open } from 'node:fs/promises';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { authorize, authorizeGitUpdate } from './config.mjs';
import { commandDefinition, parseMention, responsePayload, helpText } from './commands.mjs';
import { GitService } from './git.mjs';

export function taskDate(timeZone, date = new Date()) {
  return new Intl.DateTimeFormat('en-CA', { timeZone, year: 'numeric', month: '2-digit', day: '2-digit' }).format(date);
}

export async function dispatch(config, service, git, context, command) {
  const key = `${context.guildId}:${context.channelId}`;
  switch (command.kind) {
    case 'help': return { text: helpText };
    case 'stop': return { text: await service.stop(key) };
    case 'reset': return { text: await service.reset(key) };
    case 'git':
      if (['fetch', 'pull'].includes(command.action) && !authorizeGitUpdate(config, context.userId)) {
        throw new Error('Fetch и pull доступны пользователям из operatorUserIds при включённом allowGitUpdates.');
      }
      return { text: await service.queue.run(() => git.execute(command.action)), filename: 'git.txt' };
    default: {
      if (!command.text?.trim() || command.text.length > 6000) throw new Error('Вопрос должен содержать от 1 до 6000 символов.');
      const text = await service.ask(key, { id: context.userId, name: context.userName }, command.text, command.kind);
      if (command.kind !== 'task') return { text };
      const filename = `task-${taskDate(config.timeZone)}-${randomUUID().slice(0, 8)}.md`;
      const directory = path.join(config.stateRoot, 'tasks');
      await mkdir(directory, { recursive: true });
      await writeFile(path.join(directory, filename), text, { encoding: 'utf8', flag: 'wx' });
      return { text, filename, attach: true };
    }
  }
}

export async function runBot(config, service) {
  const token = process.env.DISCORD_BOT_TOKEN;
  if (!token) throw new Error('Нет токена Discord. Запусти Setup.ps1, затем Start.ps1.');
  // The token stays in this process only, not in Codex/Git child environments.
  delete process.env.DISCORD_BOT_TOKEN;
  const publicError = error => String(error.message ?? error).split(token).join('[скрыто]').slice(0, 1600);
  const lockPath = path.join(config.stateRoot, 'bot.lock');
  let lock;
  try { lock = await open(lockPath, 'wx'); }
  catch (error) {
    if (error.code === 'EEXIST') throw new Error('Бот уже запущен или остался bot.lock после сбоя. Закрой предыдущий процесс; затем удали Saved/DiscordCodex/bot.lock.');
    throw error;
  }
  await lock.writeFile(String(process.pid));
  const client = new Client({ intents: [GatewayIntentBits.Guilds, GatewayIntentBits.GuildMessages] });
  const git = new GitService(config.projectRoot);
  const contextOf = (item, user) => ({
    guildId: item.guildId, channelId: item.channelId, parentId: item.channel?.parentId,
    userId: user.id, userName: user.globalName ?? user.username, bot: user.bot,
  });
  const replyError = async (reply, error) => reply(responsePayload(`Ошибка: ${publicError(error)}`));

  client.on(Events.InteractionCreate, async interaction => {
    if (!interaction.isChatInputCommand() || interaction.commandName !== 'mc') return;
    const context = contextOf(interaction, interaction.user);
    if (!authorize(config, context)) {
      await interaction.reply({ content: 'Этот сервер, канал или пользователь не включён в настройки бота.', flags: MessageFlags.Ephemeral }).catch(() => {});
      return;
    }
    let status;
    try {
      await interaction.deferReply();
      status = await interaction.editReply(responsePayload('Проверяю…'));
      const kind = interaction.options.getSubcommand();
      const command = { kind, text: interaction.options.getString('question')
        ?? interaction.options.getString('query') ?? interaction.options.getString('description'),
      action: interaction.options.getString('action') };
      const result = await dispatch(config, service, git, context, command);
      // Use bot-authenticated message editing so a queued answer survives the
      // interaction webhook's 15-minute lifetime.
      await status.edit(responsePayload(result.text, result.filename, result.attach));
    } catch (error) {
      const reply = status ? payload => status.edit(payload) : interaction.deferred || interaction.replied
        ? payload => interaction.editReply(payload) : payload => interaction.reply({ ...payload, flags: MessageFlags.Ephemeral });
      await replyError(reply, error).catch(() => console.error('Не удалось отправить ответ Discord.'));
    }
  });

  client.on(Events.MessageCreate, async message => {
    const context = contextOf(message, message.author);
    if (!authorize(config, context) || message.webhookId || message.system) return;
    let status;
    try {
      const command = parseMention(message.content, client.user.id);
      if (!command) return;
      status = await message.reply(responsePayload('Проверяю…'));
      const result = await dispatch(config, service, git, context, command);
      await status.edit(responsePayload(result.text, result.filename, result.attach));
    } catch (error) {
      await replyError(payload => status ? status.edit(payload) : message.reply(payload), error)
        .catch(() => console.error('Не удалось отправить ответ Discord.'));
    }
  });
  client.on(Events.Error, error => console.error(`Discord: ${publicError(error)}`));
  client.once(Events.ClientReady, async ready => {
    try {
      const definition = commandDefinition(SlashCommandBuilder).toJSON();
      for (const guildId of config.allowedGuildIds) {
        // Upsert this one command; do not overwrite other commands of an existing app.
        await ready.application.commands.create(definition, guildId);
      }
      console.log(`Бот ${ready.user.tag} готов. Команды /mc установлены. Ctrl+C — остановка.`);
    } catch (error) {
      console.error(`Регистрация /mc: ${publicError(error)}. Проверь установку бота на сервер и scope applications.commands.`);
    }
  });
  const cleanup = async () => {
    client.destroy(); service.close(); await lock.close();
    const { unlink } = await import('node:fs/promises');
    await unlink(lockPath).catch(() => {});
  };
  const shutdown = () => { cleanup().finally(() => process.exit(0)); };
  process.once('SIGINT', shutdown); process.once('SIGTERM', shutdown);
  try {
    await service.initialize();
    await client.login(token);
  } catch (error) {
    process.off('SIGINT', shutdown); process.off('SIGTERM', shutdown);
    await cleanup(); throw new Error(publicError(error));
  }
}
