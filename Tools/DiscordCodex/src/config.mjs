import { readFile, mkdir } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const toolRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const defaultProjectRoot = path.resolve(toolRoot, '../..');
export const defaultConfigPath = path.join(defaultProjectRoot, 'Saved/DiscordCodex/config.json');

function ids(value, field, required = false) {
  if (!Array.isArray(value) || value.some(id => typeof id !== 'string' || !/^\d{17,22}$/.test(id))) {
    throw new Error(`${field}: нужен массив Discord ID в виде строк.`);
  }
  if (required && !value.length) throw new Error(`${field}: укажи хотя бы один Discord ID.`);
  return [...new Set(value)];
}

export function validateConfig(raw, configPath = defaultConfigPath, requireDiscord = true) {
  const projectRoot = path.resolve(path.dirname(configPath), raw.projectRoot ?? defaultProjectRoot);
  if (!existsSync(path.join(projectRoot, '.git'))) throw new Error(`Не найден Git-проект: ${projectRoot}`);
  const allowedGuildIds = ids(raw.allowedGuildIds ?? [], 'allowedGuildIds', requireDiscord);
  const allowedUserIds = ids(raw.allowedUserIds ?? [], 'allowedUserIds', requireDiscord);
  const operatorUserIds = ids(raw.operatorUserIds ?? [], 'operatorUserIds');
  if (operatorUserIds.some(id => !allowedUserIds.includes(id))) {
    throw new Error('operatorUserIds должны входить в allowedUserIds.');
  }
  const turnTimeoutMs = raw.turnTimeoutMs ?? 300000;
  if (!Number.isInteger(turnTimeoutMs) || turnTimeoutMs < 1000 || turnTimeoutMs > 600000) {
    throw new Error('turnTimeoutMs должен быть от 1000 до 600000.');
  }
  const maxQueuedRequests = raw.maxQueuedRequests ?? 6;
  if (!Number.isInteger(maxQueuedRequests) || maxQueuedRequests < 0 || maxQueuedRequests > 20) {
    throw new Error('maxQueuedRequests должен быть от 0 до 20.');
  }
  if (raw.allowGitUpdates !== undefined && typeof raw.allowGitUpdates !== 'boolean') {
    throw new Error('allowGitUpdates должен быть true или false.');
  }
  if (raw.codexExecutable !== undefined && (typeof raw.codexExecutable !== 'string' || !raw.codexExecutable.trim())) {
    throw new Error('codexExecutable должен содержать путь или имя Codex.');
  }
  const timeZone = raw.timeZone ?? 'Asia/Yakutsk';
  try { new Intl.DateTimeFormat('ru-RU', { timeZone }); }
  catch { throw new Error('timeZone должен содержать действительный часовой пояс IANA.'); }
  return {
    projectRoot, codexExecutable: raw.codexExecutable ?? 'codex',
    allowedGuildIds, allowedUserIds, operatorUserIds,
    allowedChannelIds: ids(raw.allowedChannelIds ?? [], 'allowedChannelIds'),
    allowGitUpdates: raw.allowGitUpdates === true, turnTimeoutMs, maxQueuedRequests, timeZone,
    stateRoot: path.join(projectRoot, 'Saved/DiscordCodex'),
  };
}

export async function loadConfig(configPath = defaultConfigPath, requireDiscord = true) {
  let raw = {};
  try {
    raw = JSON.parse((await readFile(configPath, 'utf8')).replace(/^\uFEFF/, ''));
  } catch (error) {
    if (error.code !== 'ENOENT' || requireDiscord) {
      if (error.code === 'ENOENT') throw new Error('Сначала запусти Tools/DiscordCodex/Setup.ps1.');
      throw error;
    }
  }
  const config = validateConfig(raw, path.resolve(configPath), requireDiscord);
  await mkdir(config.stateRoot, { recursive: true });
  return config;
}

export function authorize(config, { guildId, channelId, parentId, userId, bot = false }) {
  return !bot && config.allowedGuildIds.includes(guildId) && config.allowedUserIds.includes(userId)
    && (!config.allowedChannelIds.length || config.allowedChannelIds.includes(channelId)
      || config.allowedChannelIds.includes(parentId));
}

export function authorizeGitUpdate(config, userId) {
  return config.allowGitUpdates && config.operatorUserIds.includes(userId);
}
