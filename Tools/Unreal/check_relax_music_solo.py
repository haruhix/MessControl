"""Twelve-second native startup check; no controller audio override or DA edits.

Resolves the opening run-only perk choice so actual gameplay advances. Checks
the actual source and persistent voice across automatic Relax -> Gameplay.
Master mixer WAV is verified separately for complete late non-silent output.
"""
from pathlib import Path
scope=__doc__;code=Path(__file__).with_name('diagnose_relax_music_solo.py').read_text(encoding='utf-8').rsplit('builtins.mc_relax_diagnosis=RelaxDiagnosis()',1)[0]
code=code.replace("if self.phase=='automatic' and age>8:","if False:").replace('if age>17:self.finish()','if age>12:self.finish()').replace("'Diagnosis'","'AfterFix'").replace("'Diagnosis.json'","'AfterFix.json'")
exec(compile(code,str(Path(__file__).with_name('diagnose_relax_music_solo.py')),'exec'));__doc__=scope

class RelaxCheck(RelaxDiagnosis):
 def tick(self,dt):
  if self.pc and not self.done:
   ps=self.pc.player_state;offer=ps.get_editor_property('LevelUpOffer') if ps else None
   if offer and len(offer.get_editor_property('PerkIDs'))==3:self.pc.call_method('ServerChooseLevelPerk',args=(offer.get_editor_property('OfferId'),0))
   if u.GameplayStatics.is_game_paused(self.world):
    self.pc.call_method('ServerSetPerkChoicePause',args=(False,));u.GameplayStatics.set_game_paused(self.world,False)
  super().tick(dt)
 def finish(self,error=None):
  checks={}
  if not error:
   try:
    sources=[v for r in self.rows for v in r['voices'] if v['sound']=='/Game/Audio/Relax.Relax']
    checks={'automatic_relax_and_gameplay_observed':{'Relax','Gameplay'}.issubset({r['context'] for r in self.rows}),'same_voice_without_restart':len({v['component'] for v in sources})==1,'music_still_playing_after_eleven_seconds':any(r['time']>11 and any(v['sound']=='/Game/Audio/Relax.Relax' and v['playing'] for v in r['voices']) for r in self.rows),'no_fade_out_on_startup_context_change':all('FADING_OUT' not in v['state'] for v in sources),'actual_gameplay_clock_advanced':self.rows[-1]['world_time']>8}
    assert all(checks.values()),checks
   except Exception:error=traceback.format_exc()
  super().finish(error)
  report=self.out/'AfterFix.json';d=json.loads(report.read_text());d['checks']=checks;report.write_text(json.dumps(d,indent=2));print('RELAX_CHECK_RESULT',checks)

builtins.mc_relax_check=RelaxCheck()
