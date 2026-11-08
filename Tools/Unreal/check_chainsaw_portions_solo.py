"""One owned solo: F3 chainsaw -> real primary input -> independent food cuts.

Run only in the rebuilt -MCHubTransientProfile Editor on L_Mouth. Pass
MC_SAW_LOGFILE (actual Editor log on E:) and optionally MC_SAW_OUTPUT.
Uses the saved-scale mixed Strawberry fixture and loading barrier from the
island recipe. No direct SawContact/HitFood, movement freeze, asset/profile
saves, rescaling or extra peers. Cancel via builtins.mc_chainsaw_portions_solo.
"""
import builtins
import hashlib
import re
import runpy
import time
import traceback
from pathlib import Path

import unreal as u

ROOT = Path('E:/DEVGAME/MessControl')
OUTPUT = globals().get('MC_SAW_OUTPUT', 'E:/CodexWork/MessControl/Validation/Day1Start_20261108/SawSolo')
LOGFILE = globals().get('MC_SAW_LOGFILE')
support = runpy.run_path(str(ROOT/'Tools/Unreal/check_food_islands_solo.py'),
    run_name='chainsaw_portion_support', init_globals={'MC_ISLAND_OUTPUT': OUTPUT, 'MC_ISLAND_LOGFILE': LOGFILE})
Parent, prop, vec, valid, path, BATCH = (support[k] for k in ('FoodIslandsSolo', 'prop', 'vec', 'valid', 'path', 'BATCH'))
KEY = 'mc_chainsaw_portions_solo'


class ChainsawPortionsSolo(Parent):
    def __init__(self):
        super().__init__()
        self.report.update(mode='chainsaw_portions', scope='One owned solo, real F3 chainsaw and primary input on unchanged saved-scale food')
        self.report['source_sha256']['Source/MessControl/Private/MCInventoryUpgrades.cpp'] = hashlib.sha256((ROOT/'Source/MessControl/Private/MCInventoryUpgrades.cpp').read_bytes()).hexdigest()
        self.first_stage = None
        self.second_stage_at = None
        self.running_seen = False
        self.first_large = []
        self.report['limitations'].append('The actual chainsaw keeps its normal latched forward motion. The test ends its owned PIE after observing a second accepted geometric contact; it does not replace the wall-stop, steering or tool-lock mechanics.')
        self.report['limitations'].append('CutPlanes is protected and FMCFoodCutPlane has no Python wrapper in this build. The recipe verifies a second accepted transaction from an actual previously large CPU fragment plus newly reconstructed procedural mesh geometry, not direct private plane-field inspection.')

    def fixture(self, kind):
        super().fixture(kind)
        # Ordinary playtests intentionally keep MC.DevUI disabled. Open the
        # real F3 panel for its temporary grant, then return to gameplay input.
        self.pc.toggle_dev_panel()
        try:
            self.pc.call_method('RequestDevAction', args=(u.MCDevAction.GRANT_TOOL_BOOSTER, 2))
        finally:
            self.pc.toggle_dev_panel()
        self.require(prop(self.hero, 'inventory').has_upgrade(u.MCToolUpgrade.CHAINSAW), 'F3 did not grant the real temporary chainsaw')
        self.start_position = self.hero.get_actor_location()
        self.report['temporary_upgrade'] = 'Real F3 GrantToolBooster / Chainsaw, current pawn only'
        self.stage('saw_before')

    def stages(self):
        with Path(LOGFILE).open('rb') as stream:
            stream.seek(self.log_offset)
            lines = stream.read().decode('utf8', errors='replace').splitlines()
        result = []
        for line in lines:
            if 'MC_FOOD_ISLANDS_STAGED' in line and 'batch='+str(BATCH) in line:
                match = re.search(r'source=(\S+).*parts=(\d+) ready=(\d+) mass=([\d.eE+-]+) units=(\d+)', line)
                if match:
                    result.append(dict(source=match[1], parts=int(match[2]), ready=int(match[3]), mass=float(match[4]), units=int(match[5]), raw=line))
        return result

    def part_records(self):
        result = []
        for food in self.actors(u.MCFoodActor):
            if int(prop(food, 'batch')) != BATCH or food == self.food:
                continue
            triangles = 0
            for component in food.get_components_by_class(u.ProceduralMeshComponent):
                for section in range(component.get_num_sections()):
                    _, indices, _, _, _ = u.ProceduralMeshLibrary.get_section_from_procedural_mesh(component, section)
                    triangles += len(indices)//3
            result.append(dict(actor=path(food), name=food.get_name(), ready=bool(food.fits_automatic_intake_portion()),
                disposed=prop(food, 'phase') == u.MCFoodPhase.DISPOSED, hidden=bool(prop(food, 'hidden')),
                size_cm=vec(food.get_hand_portion_size()), mass=float(prop(prop(food, 'food_data'), 'mass')),
                procedural_triangles=triangles))
        return result

    def tick(self, delta):
        if self.done:
            return
        if self.phase == 'waiting_pie':
            return super().tick(delta)
        try:
            self.require(time.monotonic()-self.started < 210, 'Owned chainsaw solo exceeded 210 wall seconds')
            self.require(self.owned_pie_is_active(), 'Owned chainsaw PIE was externally stopped/replaced')
            now = float(u.GameplayStatics.get_time_seconds(self.world))
            if self.last_game_time is not None and now <= self.last_game_time:
                return
            self.last_game_time = now
            inventory = prop(self.hero, 'inventory')
            self.running_seen |= bool(inventory.is_chainsaw_running())
            if now >= self.sample_at:
                self.sample_at = now+.08
                self.observe(now)
                self.report['samples'][-1].update(chainsaw_running=bool(inventory.is_chainsaw_running()), hero_location=vec(self.hero.get_actor_location()))
            if now >= self.frame_at:
                self.frame_at = now+1/self.capture_hz
                self.screenshot('%05d.png' % len(self.report['frames']), True)
            elapsed = now-self.phase_at
            if self.phase == 'saw_before':
                self.require(elapsed < 10, 'Powered-knife selection/readiness did not settle')
                if elapsed >= 2 and inventory.selected == u.MCToolSlot.KNIFE and self.work_state()['ready']:
                    self.primary_input(True)
                    self.stage('saw_cut')
            elif self.phase == 'saw_cut':
                self.require(elapsed < 3, 'Continuous saw did not produce two accepted contacts on real food')
                stages = self.stages()
                parts = self.part_records()
                if stages and self.first_stage is None:
                    self.first_stage = stages[0]
                    self.require(0 < self.first_stage['ready'] < self.first_stage['parts'], 'First saw contact must independently resolve small and large pieces')
                    self.first_large = [p for p in parts if not p['ready'] and not p['disposed']]
                    self.require(self.first_large, 'First contact left no large piece to re-cut')
                    self.report['first_parts'] = parts
                    self.report['checks'].update(small_parts_complete_independently=True, large_remainders_remain=True,
                        initial_ready_count=self.first_stage['ready'], initial_large_count=len(self.first_large))
                if len(stages) >= 2:
                    self.require(self.running_seen, 'The real chainsaw never entered its running state')
                    previous_names = [p['name'] for p in self.report['first_parts']]
                    geometric = [p for p in parts if p['name'] not in previous_names and p['procedural_triangles'] > 0]
                    self.require(geometric, 'Second saw contact did not reconstruct new procedural cut geometry')
                    self.require(any(s['source'] in [p['name'] for p in self.first_large] for s in stages[1:]), 'The continuous saw did not re-cut a previously large remainder')
                    self.report['accepted_stages'] = stages
                    self.report['recut_parts'] = parts
                    self.report['checks'].update(real_primary_started_chainsaw=True, continuous_recut_uses_geometry=True,
                        accepted_contact_count=len(stages), normal_forward_motion=sum((a-b)**2 for a, b in zip(vec(self.hero.get_actor_location()), vec(self.start_position))) > 100)
                    self.primary_input(False)
                    self.stage('saw_tail')
            elif self.phase == 'saw_tail' and elapsed >= .8:
                accepted = self.intake_log_lines()
                names = [re.search(r'part=(\S+)', line)[1] for line in accepted]
                self.require(accepted and len(set(names)) == len(names), 'Accepted saw portions are missing or duplicated')
                units = sum(int(re.search(r'units=(\d+)', line)[1]) for line in accepted)
                self.require(units <= self.first_stage['units'], 'Saw re-cuts created extra source award units')
                active = [n for n in self.report['niagara'] if n['active'] and 'MCPerkVFXCompositionPlayer_' in n['component']]
                self.require(active, 'No actual active AutoMouth Niagara was observed')
                self.require(self.report['checks']['normal_forward_motion'], 'Fixture suppressed normal saw locomotion')
                self.report['checks'].update(no_duplicate_intake=True, original_award_budget_preserved=True,
                    actual_active_niagara=True, gameplay_camera_retained=self.pc.get_view_target() == self.hero)
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())


def main():
    old = getattr(builtins, KEY, None)
    if old is not None and not getattr(old, 'done', True):
        raise RuntimeError('An owned chainsaw portion check is already active')
    probe = ChainsawPortionsSolo()
    setattr(builtins, KEY, probe)
    try:
        probe.start()
    except Exception:
        probe.finish(traceback.format_exc())
        raise


if __name__ == '__main__':
    main()
