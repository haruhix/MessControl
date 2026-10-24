(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.AudioCandidates = api;
})(typeof window === 'object' ? window : globalThis, function () {
  'use strict';
  const EPSILON = 1e-6;
  const finite = value => value !== null && value !== '' && Number.isFinite(Number(value));
  function policyFor(slot = {}) {
    const source = {...slot.search_keywords, ...slot.timing_policy};
    const kind = String(slot.kind || slot.type || 'sfx').toLowerCase();
    const fallbackMin = kind === 'music' ? 30 : kind === 'ambience' ? 2 : .025;
    const fallbackMax = kind === 'music' ? 1800 : kind === 'ambience' ? 900 : 3.5;
    const valid = key => finite(source[key]) && Number(source[key]) >= 0;
    const min = valid('min_duration') ? Number(source.min_duration) : fallbackMin;
    const max = Math.max(min, valid('max_duration') ? Number(source.max_duration) : fallbackMax);
    const clamp = value => Math.max(min, Math.min(max, value));
    const idealMin = clamp(finite(source.ideal_min) ? Number(source.ideal_min) : min);
    const idealMax = Math.max(idealMin, clamp(finite(source.ideal_max) ? Number(source.ideal_max) : max));
    return {min_duration:min, max_duration:max, ideal_min:idealMin, ideal_max:idealMax,
      mode:['one_shot','loop','music'].includes(source.mode) ? source.mode : kind === 'music' ? 'music' : slot.loop ? 'loop' : 'one_shot',
      reason:source.reason || 'Длительность выбирается под характер игрового события.'};
  }
  function timingFit(slot, file, clip = null) {
    const policy = policyFor(slot);
    const source = finite(file?.duration) && Number(file.duration) > 0 ? Number(file.duration) : null;
    const hasStart = clip?.trim_start !== undefined && clip.trim_start !== null && clip.trim_start !== '';
    const hasEnd = clip?.trim_end !== undefined && clip.trim_end !== null && clip.trim_end !== '';
    const start = hasStart ? Number(clip.trim_start) : 0;
    const end = hasEnd ? Number(clip.trim_end) : source;
    const duration = end === null ? null : (source === null ? end : Math.min(end, source)) - start;
    const result = {...policy, source_duration:source, duration, known:duration !== null && duration > 0,
      fits:false, score:0, code:'unknown_duration', reason:'Длительность неизвестна — нужен анализ файла.'};
    if (!file) return {...result, known:false, code:'missing_file', reason:'Файл отсутствует в библиотеке.'};
    if ((hasStart && !finite(clip.trim_start)) || (hasEnd && !finite(clip.trim_end)) || start < 0 || (end !== null && end <= start) || (source !== null && start >= source))
      return {...result, known:false, code:'invalid_trim', reason:'Некорректные границы фрагмента.'};
    if (!result.known) return result;
    if (duration < policy.min_duration - EPSILON) return {...result, code:'too_short', reason:'Короче рекомендуемого диапазона.'};
    if (duration > policy.max_duration + EPSILON) return {...result, code:'too_long', reason:'Длиннее рекомендуемого диапазона.'};
    let score = 1;
    if (duration < policy.ideal_min) score = .5 + .5 * (duration-policy.min_duration) / Math.max(EPSILON, policy.ideal_min-policy.min_duration);
    if (duration > policy.ideal_max) score = .5 + .5 * (policy.max_duration-duration) / Math.max(EPSILON, policy.max_duration-policy.ideal_max);
    return {...result, fits:true, score:Math.max(.5, Math.min(1, score)), code:'fits', reason:'Подходит по длительности.'};
  }
  function tokens(value) {
    return String(value || '').replace(/([a-z])([A-Z])/g,'$1 $2').toLowerCase().match(/[a-zа-яё]+/g) || [];
  }
  function termMatch(term, words) {
    const parts = tokens(term);
    return parts.length > 0 && parts.every(part => words.some(word => word === part || (part.length >= 3 && word.startsWith(part))));
  }
  function contextScore(slot = {}, file = {}) {
    const search = slot.search_keywords || {};
    const positive = search.positive || [];
    const negative = search.negative || [];
    const nameWords = tokens(file.name);
    const parts = String(file.relative_path || '').replace(/\\/g,'/').split('/');
    // Broad collection categories ("General Foley Wet Hits") are not evidence for every file.
    const meaningfulFolders = parts.length > 3 ? parts.slice(2,-1).join('/') : '';
    const folderWords = tokens(meaningfulFolders);
    if (negative.some(term => termMatch(term,nameWords) || termMatch(term,folderWords))) return -1;
    if (!positive.length) return 1;
    let score = 0;
    for (const term of positive) {
      if (termMatch(term,nameWords)) score += 12;
      else if (termMatch(term,folderWords)) score += 4;
      else if (tokens(term).some(word=>word.length>=4) && termMatch(term,tokens(file.pack))) score += 1;
    }
    return score;
  }
  function rangeLabel(slot) {
    const p = policyFor(slot);
    const fmt = value => Number(value.toFixed(3)).toLocaleString('ru-RU');
    return `${fmt(p.min_duration)}–${fmt(p.max_duration)} сек`;
  }
  return {policyFor, timingFit, contextScore, rangeLabel};
});
