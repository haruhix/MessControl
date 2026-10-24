const test = require('node:test');
const assert = require('node:assert/strict');
const {policyFor, timingFit, contextScore, rangeLabel} = require('./web/candidates.js');

test('partial policies inherit per-field legacy limits and custom type defaults', () => {
  const rule = policyFor({type:'ambience', search_keywords:{min_duration:4,max_duration:120},timing_policy:{ideal_min:10}});
  assert.equal(rule.min_duration,4);
  assert.equal(rule.max_duration,120);
  assert.equal(rule.ideal_min,10);
  assert.equal(policyFor({type:'ambience'}).max_duration,900);
  const highMinimum = policyFor({timing_policy:{min_duration:20}});
  assert.equal(highMinimum.max_duration,20);
});

test('missing files cannot become candidates through an explicit crop', () => {
  const fit = timingFit({kind:'sfx'},undefined,{trim_start:0,trim_end:.2});
  assert.equal(fit.fits,false);
  assert.equal(fit.code,'missing_file');
});

// Explicit fixtures keep regressions independent of future catalogue tuning.
const STEP = {
  id: 'event.Step', kind: 'sfx',
  search_keywords: {positive: ['step', 'footstep', 'wet'], negative: ['loop', 'music']},
  timing_policy: {min_duration: .04, max_duration: .65, ideal_min: .12, ideal_max: .45, mode: 'one_shot'}
};
const step = (duration, extra = {}) => ({
  name: 'footstep_wet_01.wav', duration,
  relative_path: '01_General_Foley_Wet_Hits/ChequeredInk_400Sounds/Footsteps/footstep_wet_01.wav',
  pack: 'ChequeredInk_400Sounds', ...extra
});

test('explicit timing policy takes precedence over legacy search duration', () => {
  const slot = {...STEP, search_keywords: {...STEP.search_keywords, min_duration: 20, max_duration: 100}};
  assert.equal(policyFor(slot).max_duration, .65);
  assert.equal(timingFit(slot, step(.3)).fits, true);
  assert.equal(timingFit(slot, step(20)).code, 'too_long');
});

test('legacy duration policies and music/ambience fallback remain usable', () => {
  const legacy = {search_keywords: {min_duration: 2, max_duration: 10}};
  assert.equal(timingFit(legacy, {duration: 2}).fits, true);
  assert.equal(timingFit(legacy, {duration: 1.9}).code, 'too_short');
  assert.equal(policyFor({kind: 'music'}).min_duration, 30);
  assert.equal(policyFor({kind: 'ambience'}).min_duration, 2);
});

test('inclusive timing boundaries and epsilon do not lose valid short clips', () => {
  for (const duration of [.04, .65, .04 - 1e-7, .65 + 1e-7]) {
    assert.equal(timingFit(STEP, step(duration)).fits, true, String(duration));
  }
  assert.equal(timingFit(STEP, step(.039)).code, 'too_short');
  assert.equal(timingFit(STEP, step(.651)).code, 'too_long');
});

test('unknown, zero, negative, NaN and infinite source durations are excluded', () => {
  for (const duration of [undefined, null, '', 0, -1, NaN, Infinity, -Infinity, 'not a duration']) {
    const result = timingFit(STEP, step(duration));
    assert.equal(result.fits, false, String(duration));
    assert.equal(result.known, false, String(duration));
    assert.equal(result.code, 'unknown_duration', String(duration));
  }
});

test('valid numeric metadata can be read from JSON-compatible text', () => {
  assert.equal(timingFit(STEP, step('0.3')).duration, .3);
  assert.equal(timingFit(STEP, step('0.3')).fits, true);
});

test('ideal durations outrank equally relevant clips close to hard limits', () => {
  const ideal = timingFit(STEP, step(.25));
  assert.equal(ideal.score, 1);
  assert.ok(ideal.score > timingFit(STEP, step(.62)).score);
  assert.ok(ideal.score > timingFit(STEP, step(.05)).score);
  assert.equal(timingFit(STEP, step(.04)).score, .5);
  assert.equal(timingFit(STEP, step(.65)).score, .5);
});

test('a matching name never rescues an eight-second source for a footstep', () => {
  const long = step(8);
  const short = step(.25);
  assert.equal(contextScore(STEP, long), contextScore(STEP, short));
  assert.ok(contextScore(STEP, long) > 0);
  assert.equal(timingFit(STEP, long).fits, false);
  assert.equal(timingFit(STEP, long).code, 'too_long');
  assert.equal(timingFit(STEP, short).fits, true);
});

test('manual trim validates the effective segment instead of the source length', () => {
  const cropped = timingFit(STEP, step(12), {trim_start: 2, trim_end: 2.3});
  assert.equal(cropped.source_duration, 12);
  assert.ok(Math.abs(cropped.duration - .3) < 1e-10);
  assert.equal(cropped.fits, true);
  assert.equal(timingFit(STEP, step(12), {trim_start: 2, trim_end: null}).code, 'too_long');
  assert.equal(timingFit(STEP, step(12), {trim_start: 2, trim_end: 2.01}).code, 'too_short');
});

test('trim end is clamped to available audio and reversed/negative clips never fit', () => {
  const truncated = timingFit(STEP, step(.5), {trim_start: .1, trim_end: 2});
  assert.equal(truncated.duration, .4);
  assert.equal(truncated.fits, true);
  for (const clip of [{trim_start: 1}, {trim_start: -1, trim_end: .3},
                     {trim_start: .3, trim_end: .2}, {trim_start: .3, trim_end: .3}]) {
    assert.equal(timingFit(STEP, step(.5), clip).fits, false, JSON.stringify(clip));
  }
});

test('malformed explicit trim fields cannot silently fall back to the entire file', () => {
  for (const invalid of [NaN, Infinity, -Infinity, 'not a time']) {
    for (const clip of [{trim_start: invalid}, {trim_start: 0, trim_end: invalid}]) {
      const fit = timingFit(STEP, step(.3), clip);
      assert.equal(fit.fits, false, String(invalid));
      assert.equal(fit.known, false, String(invalid));
      assert.equal(fit.code, 'invalid_trim', String(invalid));
    }
  }
});

test('an explicit valid manual segment can be reviewed when source metadata is unknown', () => {
  const fit = timingFit(STEP, step(null), {trim_start: 2, trim_end: 2.3});
  assert.equal(fit.source_duration, null);
  assert.equal(fit.known, true);
  assert.equal(fit.fits, true);
  assert.ok(Math.abs(fit.duration - .3) < 1e-10);
  assert.equal(timingFit(STEP, step(null), {trim_start: 2, trim_end: null}).code, 'unknown_duration');
  assert.equal(timingFit(STEP, step(null), {trim_start: 2, trim_end: 2.01}).code, 'too_short');
  assert.equal(timingFit(STEP, step(null), {trim_start: 2, trim_end: 5}).code, 'too_long');
});

test('collection category Wet does not qualify unrelated card draws as steps', () => {
  const card = {name: 'card_draw_1.wav', duration: .3483,
    relative_path: '01_General_Foley_Wet_Hits/ChequeredInk_400Sounds/Card and Board/card_draw_1.wav',
    pack: 'ChequeredInk_400Sounds', category: '01_General_Foley_Wet_Hits'};
  assert.equal(timingFit(STEP, card).fits, true);
  assert.equal(contextScore(STEP, card), 0);
  assert.ok(contextScore(STEP, step(.3)) > contextScore(STEP, card));
});

test('semantic folders are useful while negative loops remain excluded', () => {
  const genericFootstep = step(.3, {name: 'take_01.wav'});
  const unrelated = step(.3, {name: 'door_creak.wav', relative_path: 'Foley/Pack/Doors/door_creak.wav'});
  assert.ok(contextScore(STEP, genericFootstep) > 0);
  assert.equal(contextScore(STEP, unrelated), 0);
  assert.equal(contextScore(STEP, step(.3, {name: 'footstep_loop.wav'})), -1);
  assert.equal(contextScore(STEP, step(.3, {relative_path: 'Foley/Pack/Music/footstep_wet_01.wav'})), -1);
});

test('candidate gate requires both relevant context and an acceptable duration', () => {
  const files = [step(8), step(.3), step(.3, {name: 'card_draw_1.wav',
    relative_path: '01_General_Foley_Wet_Hits/Pack/Card and Board/card_draw_1.wav'}),
    step(null), step(.3, {name: 'footstep_loop.wav'})];
  const eligible = files.filter(file => timingFit(STEP, file).fits && contextScore(STEP, file) > 0);
  assert.deepEqual(eligible, [files[1]]);
});

test('range label presents the active limits to the reviewer', () => {
  const label = rangeLabel(STEP);
  assert.match(label, /0[,.]04/);
  assert.match(label, /0[,.]65/);
  assert.match(label, /сек/);
});
