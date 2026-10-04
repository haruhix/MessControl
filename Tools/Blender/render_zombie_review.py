"""Fast textured pose/face playblast from the derived boss animation scene.

Does not save a Blender scene or change any exported mesh/animation FBX.
"""
import bpy,json,math,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/ZombieBossAnimations'
manifest=json.loads((OUT/'ZombieAnimationReport.json').read_text(encoding='utf8'))
assert Path(bpy.data.filepath).name=='ZombieBossAnimations.blend'
with bpy.data.libraries.load(str(OUT/'ZombieBossFace.blend'),link=False) as (src,dst):
    dst.objects=['SK_ZombieBoss','ZombieBossMouth']
meshes=[]
for loaded,name in zip(dst.objects,('SK_ZombieBoss','ZombieBossMouth')):
    obj=bpy.data.objects[name];obj.data=loaded.data;meshes.append(obj)
    for k in obj.data.shape_keys.key_blocks[1:]:k.value=0
scene=bpy.context.scene
scene.render.engine='BLENDER_WORKBENCH'
scene.render.resolution_x=800;scene.render.resolution_y=600
scene.render.resolution_percentage=100
scene.display.shading.light='STUDIO';scene.display.shading.studio_light='paint.sl'
scene.display.shading.color_type='TEXTURE'
scene.display.shading.show_shadows=True
scene.display.shading.show_cavity=True
scene.display.shading.cavity_type='BOTH'
scene.display.shading.curvature_ridge_factor=1.3
scene.display.shading.curvature_valley_factor=1.1
scene.display.shading.background_type='WORLD'
frames=ROOT/'Saved/ChestBossReview/ZombieRaisedReviewFrames'
frames.mkdir(parents=True,exist_ok=True)
filters=[]
for index,record in enumerate(manifest['clips']):
    begin=(record['review_start_frame']-1)/30
    end=(manifest['clips'][index+1]['review_start_frame']-1)/30 if index+1<len(manifest['clips']) else scene.frame_end/30
    label=record['name'].removeprefix('AN_Zombie_')
    filters.append("drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='%s / boss animation':fontcolor=white:fontsize=26:box=1:boxcolor=black@0.65:boxborderw=12:x=20:y=20:enable='between(t,%.4f,%.4f)'"%(label,begin,end))
for index,frame in enumerate(range(1,scene.frame_end+1,2)):
    scene.frame_set(frame)
    record=next((r for r in reversed(manifest['clips']) if frame>=r['review_start_frame']),manifest['clips'][0])
    t=(frame-record['review_start_frame'])/30
    kind=record['name'].removeprefix('AN_Zombie_')
    pain=math.exp(-max(0,t)*5) if kind=='Hurt' else 0
    roar=math.sin(min(1,max(0,(t-.7)/.55))*math.pi/2)*(1-min(1,max(0,(t-3.5)/1.0))) if kind=='Roar' else 0
    blink=max(0,1-abs((frame/30)%3.4-2.6)/.095)
    values={'Mouth_Pain':pain,'Mouth_Roar':roar,'Eyes_Blink':blink,
            'Eyes_Squint':.10+.3*pain+.15*roar,'Brow_Angry':.5+.5*roar}
    for mesh in meshes:
        for key in mesh.data.shape_keys.key_blocks[1:]:key.value=values.get(key.name,0)
    bpy.context.view_layer.update();scene.render.filepath=str(frames/('frame_%04d.png'%index))
    bpy.ops.render.render(write_still=True)
subprocess.run(['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','error','-y',
    '-framerate','15','-i',str(frames/'frame_%04d.png'),'-vf',','.join(filters),
    '-c:v','libx264','-threads','2','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart',
    str(OUT/'ZombieBossAnimationReview.mp4')],check=True)
print('MC_ZOMBIE_REVIEW_VIDEO_PASS',scene.frame_end)
