'use strict';

(() => {
  const $ = (id) => document.getElementById(id);
  const labels = { accepted: 'Принято', rework: 'Нужна доработка', rejected: 'Отклонено', pending: 'Ждёт просмотра', passed: 'Пройдены', success: 'Пройдены', failed: 'Есть ошибки' };
  const initialQuery = new URLSearchParams(location.search);
  const state = { data: null, taskId: initialQuery.get('task'), versionId: initialQuery.get('version'), mediaId: null, family: 'all', search: '', filter: 'all', saving: false, video: null, drafts: new Map(), versions: new Map() };
  const node = (tag, cls, text) => { const n = document.createElement(tag); if (cls) n.className = cls; if (text !== undefined && text !== null) n.textContent = String(text); return n; };
  const statusLabel = (value) => labels[value] || String(value || 'Ждёт просмотра');
  const hasPreview = (v) => (v?.media || []).some((m) => m.available && mediaUrl(m.url));
  const taskStatusLabel = (t) => (t.status || 'pending') === 'pending' && !hasPreview(t.versions.at(-1)) ? 'Превью не записано' : statusLabel(t.status);
  const tone = (value) => ['passed', 'success'].includes(value) ? 'accepted' : value === 'failed' ? 'rejected' : ['accepted', 'rework', 'rejected'].includes(value) ? value : 'pending';
  const badge = (value) => node('span', `badge tone-${tone(value)}`, statusLabel(value));
  const task = () => state.data?.tasks.find((t) => t.id === state.taskId);
  const version = () => task()?.versions.find((v) => v.id === state.versionId);
  const draftKey = () => `${state.taskId}\u0000${state.versionId}`;
  const draft = () => { const k = draftKey(); if (!state.drafts.has(k)) state.drafts.set(k, { score: null, status: 'pending', comment: '', tags: [], timecodeSeconds: null, mediaLabel: '' }); return state.drafts.get(k); };
  const feedback = () => (state.data?.feedback || []).filter((f) => f.taskId === state.taskId && f.versionId === state.versionId && f.author === 'user').map((f, i) => ({ ...f, _i: i })).sort((a, b) => (Date.parse(b.createdAt) || 0) - (Date.parse(a.createdAt) || 0) || b._i - a._i);
  const timeLabel = (seconds) => { const n = Math.max(0, Math.floor(Number(seconds) || 0)); return `${String(Math.floor(n / 60)).padStart(2, '0')}:${String(n % 60).padStart(2, '0')}`; };
  const dateLabel = (value) => { const d = new Date(value); return Number.isNaN(d.valueOf()) ? '' : new Intl.DateTimeFormat('ru-RU', { day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' }).format(d); };
  function notice(message, error = false) { const n = $('notice'); n.textContent = message; n.classList.toggle('error', error); n.hidden = !message; }
  function mediaUrl(value) { try { const u = new URL(value, location.origin); return u.origin === location.origin && u.pathname.startsWith('/media/') ? u.href : null; } catch { return null; } }
  async function api(path, options) {
    const response = await fetch(path, { cache: 'no-store', credentials: 'same-origin', ...options });
    let data; try { data = await response.json(); } catch { throw new Error('Сервер вернул непонятный ответ.'); }
    if (!response.ok) throw new Error(typeof data.error === 'string' ? data.error : typeof data.message === 'string' ? data.message : `Ошибка запроса (${response.status}).`);
    return data;
  }
  function normalize(data) {
    if (!data || !Array.isArray(data.tasks) || !Array.isArray(data.feedback)) throw new Error('Не удалось прочитать список материалов.');
    return { ...data, tasks: data.tasks.map((t) => ({ ...t, versions: Array.isArray(t.versions) ? t.versions.map((v) => ({ ...v, media: Array.isArray(v.media) ? v.media : [] })) : [] })), allowedTags: Array.isArray(data.allowedTags) ? data.allowedTags.filter((s) => typeof s === 'string') : [] };
  }
  async function load() {
    $('refresh').disabled = true;
    try {
      state.data = normalize(await api('/api/state'));
      if (!task()) { state.taskId = state.data.tasks[0]?.id || null; state.versionId = null; state.mediaId = null; }
      if (initialQuery.has('task') && task() && state.family === 'all') state.family = task().family || 'all';
      if (!version() || (!hasPreview(version()) && hasPreview(task()?.versions.at(-1)))) { state.versionId = task()?.versions.at(-1)?.id || null; state.mediaId = null; }
      render();
    } finally { $('refresh').disabled = false; }
  }
  function renderFilters() {
    const select = $('status-filter'); const statuses = [...new Set(state.data.tasks.map((t) => t.status || 'pending'))];
    select.replaceChildren(new Option('Все статусы', 'all'), ...statuses.map((s) => new Option(statusLabel(s), s)));
    if (!statuses.includes(state.filter)) state.filter = 'all'; select.value = state.filter;
    const families = [...new Set(state.data.tasks.map((t) => t.family).filter(Boolean))];
    if (!families.includes(state.family)) state.family = 'all';
    const chips = [['all', 'Все'], ...families.map((f) => [f, f])].map(([id, label]) => {
      const b = node('button', `family-chip${state.family === id ? ' active' : ''}`, label); b.type = 'button'; b.setAttribute('aria-pressed', String(state.family === id));
      b.addEventListener('click', () => { state.family = id; renderFilters(); renderTaskList(); }); return b;
    }); $('family-filters').replaceChildren(...chips);
  }
  function renderTaskList() {
    const list = state.data.tasks.filter((t) => (state.family === 'all' || t.family === state.family) && (state.filter === 'all' || (t.status || 'pending') === state.filter) && `${t.title} ${t.description || ''}`.toLocaleLowerCase('ru').includes(state.search));
    $('task-count').textContent = `${list.length} / ${state.data.tasks.length}`;
    $('task-list').replaceChildren(...(list.length ? list.map((t) => {
      const b = node('button', `task-button${t.id === state.taskId ? ' active' : ''}`); b.type = 'button'; if (t.id === state.taskId) b.setAttribute('aria-current', 'page');
      b.append(node('span', 'task-button-title', t.title)); const meta = node('span', 'task-button-meta');
      const status = node('span', `status-text tone-text-${tone(t.status)}`); status.append(node('span', 'task-dot'), document.createTextNode(taskStatusLabel(t)));
      meta.append(status, node('span', '', `${t.versions.length} верс.`)); b.append(meta);
      b.addEventListener('click', () => { state.versions.set(state.taskId, state.versionId); state.taskId = t.id; state.versionId = state.versions.get(t.id) || t.versions.at(-1)?.id || null; state.mediaId = null; render(); }); return b;
    }) : [node('p', 'muted small', 'Ничего не найдено. Попробуйте другой фильтр.')]));
  }
  function summary(target, label, score, status, comment, emptyText) {
    const top = node('div', 'summary-top'); top.append(node('span', 'summary-label', label), badge(status));
    const value = node('div', 'summary-value', Number.isFinite(score) ? score : '—'); value.append(node('small', '', '/ 10'));
    const p = node('p', 'summary-comment', comment || emptyText || ''); target.replaceChildren(top, value, p);
  }
  function renderSummary() {
    const v = version(), root = v.rootReview || {}, latest = feedback()[0];
    summary($('root-summary'), 'Оценка агента', typeof root.score === 'number' ? root.score : null, root.status || 'pending', root.comment, 'Внутренняя оценка материала');
    summary($('human-summary'), 'Ваша оценка', latest?.score ?? null, latest?.status || 'pending', latest?.comment, 'Вы ещё не оценивали эту версию');
    const native = v.native || {}; const top = node('div', 'summary-top'); top.append(node('span', 'summary-label', 'Техническая проверка'), badge(native.status || 'pending'));
    const value = node('div', 'summary-value', statusLabel(native.status || 'pending'));
    const details = node('div', 'native-details', `${Number.isFinite(native.tests) ? native.tests : '—'} проверок · ${Number.isFinite(native.warnings) ? native.warnings : '—'} предупр.`);
    $('native-summary').replaceChildren(top, value, details, node('p', 'summary-comment', native.comment || 'Визуальная оценка остаётся отдельной.'));
  }
  function renderMedia() {
    if (state.video) { state.video.pause(); state.video = null; }
    const items = version().media; let selected = items.find((m) => m.id === state.mediaId);
    if (!selected) { selected = items.find((m) => m.available && mediaUrl(m.url) && m.kind === 'image' && /gif/i.test(m.label || m.id)) || items.find((m) => m.available && mediaUrl(m.url)) || items[0]; state.mediaId = selected?.id || null; }
    $('media-count').textContent = items.length ? `· ${items.length}` : '';
    $('media-tabs').replaceChildren(...items.map((m) => {
      const b = node('button', `media-tab${m.id === state.mediaId ? ' active' : ''}`, `${m.kind === 'video' ? '▶' : '▧'} ${m.label || 'Материал'}`); b.type = 'button'; b.setAttribute('role', 'tab'); b.setAttribute('aria-selected', String(m.id === state.mediaId));
      b.addEventListener('click', () => { state.mediaId = m.id; renderMedia(); }); return b;
    }));
    const stage = $('media-stage'); stage.replaceChildren(); $('capture-time').disabled = true; $('capture-time').textContent = '⌖ Добавить таймкод';
    $('open-media').hidden = true; $('open-media').removeAttribute('href'); $('media-caption').textContent = selected?.label || 'Материалов пока нет';
    const url = selected?.available ? mediaUrl(selected.url) : null;
    if (!url) { const empty = node('div', 'media-empty'); empty.append(node('strong', '', selected ? 'Материал пока недоступен' : 'Видео ещё не записано'), node('span', '', selected ? 'Файл недоступен. Нужна проверка локального пути.' : 'Задача в работе. Здесь появится запись из игры.')); stage.append(empty); return; }
    const media = document.createElement(selected.kind === 'video' ? 'video' : 'img');
    if (selected.kind === 'video') { media.controls = true; media.preload = 'metadata'; media.playsInline = true; media.setAttribute('aria-label', selected.label || 'Видео материала'); state.video = media;
      const poster = items.find((m) => m.kind === 'image' && m.available && mediaUrl(m.url));
      if (poster) media.poster = mediaUrl(poster.url);
      const update = () => { $('capture-time').disabled = media.readyState < 1; $('capture-time').textContent = `⌖ В комментарий ${timeLabel(media.currentTime)}`; };
      media.addEventListener('loadedmetadata', update); media.addEventListener('timeupdate', update);
    } else { media.alt = selected.label || 'Изображение материала'; media.loading = 'eager'; }
    media.addEventListener('error', () => { if (!stage.querySelector('.media-load-error')) stage.append(node('div', 'media-load-error', 'Не удалось открыть файл. Выберите другой материал.')); $('capture-time').disabled = true; });
    media.src = url; stage.append(media); $('open-media').href = url; $('open-media').hidden = false;
  }
  function renderForm() {
    const d = draft(); $('score-options').replaceChildren(...Array.from({ length: 11 }, (_, score) => {
      const label = node('label', 'score-option'); const radio = document.createElement('input'); radio.type = 'radio'; radio.name = 'score'; radio.value = String(score); radio.checked = d.score === score; radio.setAttribute('aria-label', `${score} из 10`);
      radio.addEventListener('change', () => { d.score = score; $('form-error').hidden = true; }); label.append(radio, document.createTextNode(String(score))); return label;
    }));
    $('feedback-status').value = d.status; $('comment').value = d.comment; $('comment-count').textContent = `${d.comment.length} / 5000`;
    $('tag-options').replaceChildren(...state.data.allowedTags.map((tag) => { const label = node('label', 'tag-option', tag); const checkbox = document.createElement('input'); checkbox.type = 'checkbox'; checkbox.value = tag; checkbox.checked = d.tags.includes(tag); checkbox.setAttribute('aria-label', tag);
      checkbox.addEventListener('change', () => { d.tags = checkbox.checked ? [...new Set([...d.tags, tag])] : d.tags.filter((t) => t !== tag); }); label.prepend(checkbox); return label; }));
    $('form-error').hidden = true; renderTimecode();
  }
  function renderTimecode() { const d = draft(), chip = $('timecode-chip'); chip.replaceChildren(); chip.hidden = d.timecodeSeconds === null;
    if (!chip.hidden) { chip.append(document.createTextNode(`⌖ ${timeLabel(d.timecodeSeconds)}`)); const b = node('button', '', '×'); b.type = 'button'; b.setAttribute('aria-label', 'Убрать привязку к таймкоду'); b.addEventListener('click', () => { d.timecodeSeconds = null; d.mediaLabel = ''; renderTimecode(); }); chip.append(b); } }
  function renderHistory() {
    const entries = feedback(); $('history-count').textContent = entries.length;
    $('feedback-history').replaceChildren(...(entries.length ? entries.map((f) => { const item = node('article', 'history-item'), top = node('div', 'history-top'); top.append(node('span', 'history-score', `${f.score}/10`), badge(f.status), node('span', 'muted', 'Вы')); const date = node('time', '', dateLabel(f.createdAt)); if (f.createdAt) date.dateTime = f.createdAt; top.append(date); item.append(top);
      if (f.comment) item.append(node('p', 'history-comment', f.comment)); const tags = node('div', 'history-tags'); for (const tag of (f.tags || [])) tags.append(node('span', 'history-tag', tag)); if (Number.isFinite(f.timecodeSeconds)) tags.append(node('span', 'history-tag', `⌖ ${timeLabel(f.timecodeSeconds)}`)); if (tags.childNodes.length) item.append(tags); return item;
    }) : [node('div', 'history-empty', 'Здесь появятся ваши отзывы по этой версии. Оценка агента выше — отдельная проверка.')]));
  }
  function render() {
    $('project-name').textContent = `${state.data.project?.name || "WASH'N'SMASH"} / REVIEW`;
    renderFilters(); renderTaskList(); const t = task(), v = version(); $('workspace').hidden = !t || !v; $('empty-state').hidden = Boolean(t && v);
    if (!t || !v) { $('empty-title').textContent = 'Материалов пока нет'; $('empty-description').textContent = 'Когда появятся задачи и версии, их можно будет посмотреть здесь.'; return; }
    $('task-title').textContent = t.title; $('task-description').textContent = t.description || ''; $('task-family').textContent = t.family || 'МАТЕРИАЛ'; $('task-status').textContent = (t.status || 'pending') === 'pending' && !hasPreview(v) ? 'Превью не записано' : statusLabel(t.status); $('task-status').className = `badge tone-${tone(t.status)}`;
    $('version-select').replaceChildren(...t.versions.map((x) => new Option(x.label || x.id, x.id))); $('version-select').value = v.id;
    const selectionUrl = new URL(location.href); selectionUrl.searchParams.set('task', t.id); selectionUrl.searchParams.set('version', v.id); history.replaceState(null, '', selectionUrl);
    renderSummary(); renderMedia(); renderForm(); renderHistory();
  }
  $('search').addEventListener('input', (e) => { state.search = e.target.value.toLocaleLowerCase('ru').trim(); if (state.data) renderTaskList(); });
  $('status-filter').addEventListener('change', (e) => { state.filter = e.target.value; if (state.data) renderTaskList(); });
  $('version-select').addEventListener('change', (e) => { state.versionId = e.target.value; state.versions.set(state.taskId, state.versionId); state.mediaId = null; render(); });
  $('refresh').addEventListener('click', () => load().then(() => notice('Данные обновлены.')).catch((e) => notice(e.message, true)));
  $('feedback-status').addEventListener('change', (e) => { draft().status = e.target.value; });
  $('comment').addEventListener('input', (e) => { draft().comment = e.target.value; $('comment-count').textContent = `${e.target.value.length} / 5000`; });
  $('capture-time').addEventListener('click', () => {
    if (!state.video || !Number.isFinite(state.video.currentTime)) return; const d = draft(), m = version().media.find((x) => x.id === state.mediaId); d.timecodeSeconds = Math.round(state.video.currentTime * 100) / 100; d.mediaLabel = m?.label || 'Видео';
    const input = $('comment'), marker = `[${d.mediaLabel} · ${timeLabel(d.timecodeSeconds)}] `, start = input.selectionStart ?? input.value.length, end = input.selectionEnd ?? start;
    const text = input.value.slice(0, start) + marker + input.value.slice(end); if (text.length <= 5000) { input.value = text; d.comment = text; input.focus(); input.setSelectionRange(start + marker.length, start + marker.length); $('comment-count').textContent = `${text.length} / 5000`; } renderTimecode();
  });
  $('feedback-form').addEventListener('submit', async (event) => {
    event.preventDefault(); if (state.saving || !version()) return; const d = draft(); const error = $('form-error');
    if (!Number.isInteger(d.score) || d.score < 0 || d.score > 10) { error.textContent = 'Выберите вашу оценку от 0 до 10.'; error.hidden = false; $('score-options').querySelector('input')?.focus(); return; }
    const payload = { taskId: state.taskId, versionId: state.versionId, score: d.score, status: d.status, comment: d.comment.trim(), tags: d.tags.filter((t) => state.data.allowedTags.includes(t)), timecodeSeconds: d.timecodeSeconds };
    state.saving = true; $('submit-feedback').disabled = true; $('submit-feedback').textContent = 'Сохраняем…'; error.hidden = true;
    try { const result = await api('/api/feedback', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
      if (!result.feedback) throw new Error('Сохранение не подтверждено сервером. Обновите историю перед повтором.');
      state.data.feedback.push(result.feedback); state.drafts.delete(`${payload.taskId}\u0000${payload.versionId}`); render(); notice('Отзыв сохранён. Спасибо — он добавлен в историю.');
      try { await load(); } catch { notice('Отзыв сохранён. Обновить остальные данные пока не удалось.'); }
    } catch (e) { error.textContent = e.message; error.hidden = false; }
    finally { state.saving = false; $('submit-feedback').disabled = false; $('submit-feedback').replaceChildren(document.createTextNode('Сохранить отзыв '), node('span', '', '↗')); }
  });
  load().catch((e) => { $('empty-title').textContent = 'Не удалось загрузить доску'; $('empty-description').textContent = 'Проверьте, что локальный сервер запущен, и нажмите ↻ в списке задач.'; notice(e.message, true); });
})();
