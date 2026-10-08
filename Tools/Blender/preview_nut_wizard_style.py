"""Render the saved Wizard style using native C++ default attack phase timings.

Only preview frames/video/report are written. The editable blend is read-only.
No gameplay overlays, VFX, collisions or movement are simulated by this playblast.
"""
import bpy,json,math,subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/NutAnimationStyle/Wizard'
FRAMES=ROOT/'Saved/NutAnimationStyle/WizardPhaseFrames'
FRAMES.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(OUT/'NutWizard_Style.blend'))
scene=bpy.context.scene;rig=bpy.data.objects['rig']
for track in rig.animation_data.nla_tracks:track.mute=True
rig.animation_data.use_nla=False
scene.render.engine='BLENDER_WORKBENCH'
scene.render.resolution_x=720;scene.render.resolution_y=720;scene.render.resolution_percentage=100
fps=15
segments=[]
def render(label,seconds,sample):
    action=bpy.data.actions['MC_Wizard_'+label]
    rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0]
    start=len(frame_records);count=round(seconds*fps)
    begin,end=action.frame_range
    for index in range(count):
        t=index/fps;p=max(0,min(1,sample(t)))
        f=begin+p*(end-begin);frame=int(f)
        scene.frame_set(frame,subframe=f-frame)
        bpy.context.view_layer.update()
        output=FRAMES/('frame_%04d.png'%len(frame_records))
        scene.render.filepath=str(output);bpy.ops.render.render(write_still=True)
        frame_records.append({'label':label,'seconds':t,'fraction':p})
    segments.append({'label':label,'start_seconds':start/fps,'end_seconds':len(frame_records)/fps})
    print('WIZARD_PHASE_PREVIEW',label,flush=True)
def cast(prep,recover):
    return lambda t: .55*t/prep if t<prep else .55+.45*(t-prep)/recover
frame_records=[]
render('Idle',3.0,lambda t:(t/3)%1)
render('Walk',3.2,lambda t:(t/1.6)%1)
render('Cast',1.8,cast(.9,.9))
render('HeavyCast',2.4,cast(1.2,1.2))
render('Summon',2.3,cast(1.1,1.2))
def rain(t):
    if t<1.2:return .55*t/1.2
    if t<5.2:return .55+.035*(math.sin((t-1.2)*3.5)+1)
    return .55+.45*(t-5.2)/1.4
render('Rain',6.6,rain)
render('Hit',.4,lambda t:t/.4)
render('Idle',.4,lambda t:(t/3)%1)
render('Death',2.4,lambda t:t/2.4)
filters=["drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Wizard / %s':fontcolor=white:fontsize=24:box=1:boxcolor=black@0.65:boxborderw=10:x=20:y=18:enable='between(t,%.4f,%.4f)'"%(r['label'],r['start_seconds'],r['end_seconds']) for r in segments]
subprocess.run(['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','error','-y','-framerate',str(fps),
    '-i',str(FRAMES/'frame_%04d.png'),'-vf',','.join(filters),'-c:v','libx264','-threads','2',
    '-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(OUT/'WizardStylePreview.mp4')],check=True)
(OUT/'PreviewReport.json').write_text(json.dumps({'video':str(OUT/'WizardStylePreview.mp4'),
    'fps':fps,'frames':len(frame_records),'duration_seconds':len(frame_records)/fps,'segments':segments,
    'timing':'Native C++ default prep/recovery, rain channel 4s; DA overrides not queried.',
    'scope':'Blender animation playblast. Game overlays, root motion, movement, VFX and networking not simulated.'},indent=2),encoding='utf8')
print('WIZARD_PHASE_PREVIEW_COMPLETE',flush=True)
