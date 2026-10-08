"""Read-only playblast of all four actual mage abilities plus lifecycle clips.

Uses native C++ preparation/recovery defaults. Instant attacks resolve and
enter recovery in the same tick, without a separate 0.1 s contact hold.
The new Melee action is a proposed DCC asset; it is not wired into the game.
"""
import bpy, json, math, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/Wizard'
FRAMES = ROOT / 'Saved/NutAnimationStyle/WizardAbilitiesV2Frames'
FRAMES.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(OUT / 'NutWizard_Style_v2.blend'))
scene = bpy.context.scene
rig = bpy.data.objects['rig']
for track in rig.animation_data.nla_tracks:
    track.mute = True
rig.animation_data.use_nla = False
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x = 720
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
fps = 24
frame_records, segments = [], []

def render(action_name, title, seconds, sample):
    action = bpy.data.actions['MC_Wizard_' + action_name]
    rig.animation_data.action = action
    rig.animation_data.action_slot = action.slots[0]
    start = len(frame_records)
    count = round(seconds * fps)
    begin, end = action.frame_range
    for index in range(count):
        t = index / fps
        fraction = max(0, min(1, sample(t)))
        f = begin + fraction * (end - begin)
        scene.frame_set(int(f), subframe=f - int(f))
        bpy.context.view_layer.update()
        output = FRAMES / ('frame_%04d.png' % len(frame_records))
        scene.render.filepath = str(output)
        bpy.ops.render.render(write_still=True)
        frame_records.append({'action': action_name, 'seconds': t, 'fraction': fraction})
    segments.append({'action': action_name, 'label': title,
                     'start_seconds': start / fps, 'end_seconds': len(frame_records) / fps})
    print('WIZARD_ABILITIES_PREVIEW', title, flush=True)

def attack(prep, recovery):
    def sample(t):
        if t < prep:
            return .55 * t / prep
        return .55 + .45 * (t - prep) / recovery
    return sample

render('Idle', 'Idle - poised and smug', 1.5, lambda t: t / 3)
render('Walk', 'Walk - cautious backsteps', 2.4, lambda t: (t / 1.6) % 1)
render('Cast', 'Ability 1 - FireNut', 1.8, attack(.9, .9))
render('Idle', 'Reset', .3, lambda t: t / 3)
render('Summon', 'Ability 2 - Summon', 2.3, attack(1.1, 1.2))
render('Idle', 'Reset', .3, lambda t: t / 3)

def rain(t):
    if t < 1.2:
        return .55 * t / 1.2
    if t < 5.2:
        return .55 + .035 * (math.sin((t - 1.2) * 3.5) + 1)
    return .55 + .45 * (t - 5.2) / 1.4

render('Rain', 'Ability 3 - NutRain - 4 second channel', 6.6, rain)
render('Idle', 'Reset', .3, lambda t: t / 3)
render('Melee', 'Ability 4 - close-range palm shove - NEW', 1.55, attack(.65, .9))
render('Idle', 'Reset', .3, lambda t: t / 3)
render('HeavyCast', 'HeavyCast - fallback clip', 2.4, attack(1.2, 1.2))
render('Hit', 'Hit reaction', .4, lambda t: t / .4)
render('Idle', 'Reset', .3, lambda t: t / 3)
render('Death', 'Death', 2.4, lambda t: t / 2.4)
filters = ["drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Wizard / %s':fontcolor=white:fontsize=22:box=1:boxcolor=black@0.65:boxborderw=10:x=20:y=18:enable='gte(t,%.4f)*lt(t,%.4f)'"
           % (s['label'], s['start_seconds'], s['end_seconds']) for s in segments]
filters.append("drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Blender animation preview / native default timings':fontcolor=white@0.85:fontsize=17:box=1:boxcolor=black@0.5:boxborderw=6:x=20:y=h-38")
video = OUT / 'Wizard_AllAbilities_v2.mp4'
subprocess.run(['C:/ffmpeg/ffmpeg.exe', '-hide_banner', '-loglevel', 'error', '-y',
                '-framerate', str(fps), '-i', str(FRAMES / 'frame_%04d.png'),
                '-vf', ','.join(filters), '-frames:v', str(len(frame_records)),
                '-c:v', 'libx264', '-threads', '2',
                '-crf', '19', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(video)], check=True)
(OUT / 'WizardAbilitiesV2PreviewReport.json').write_text(json.dumps({
    'video': str(video), 'fps': fps, 'frames': len(frame_records),
    'duration_seconds': len(frame_records) / fps, 'segments': segments,
    'timing': 'Native C++ default preparation/recovery and 4s Rain channel. Instant abilities resolve into Recovery in the same tick; no artificial executing hold. Saved DA overrides not queried.',
    'scope': 'DCC animations only. No game VFX, collision, targeting or network simulation.',
    'runtime_gap': 'Melee action requires a dedicated slot; current game reuses Cast.'}, indent=2), encoding='utf8')
print('WIZARD_ABILITIES_PREVIEW_COMPLETE', str(video), flush=True)
