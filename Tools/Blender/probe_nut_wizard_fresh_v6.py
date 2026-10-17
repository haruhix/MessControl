"""Read-only early V6 pose preview; no source save, no animation curves."""
from pathlib import Path
source = Path(__file__).with_name('author_nut_wizard_fresh_v6.py')
code = source.read_text(encoding='utf8')
code = code[:code.index('old_hashes =')] + code[code.index('# Small, asymmetrical neutral.'):code.index('records=[]')]
namespace={'__file__':str(source),'__name__':'fresh_pose_preview'}
exec(compile(code,str(source),'exec'),namespace)
bpy=namespace['bpy'];scene=namespace['scene'];apply=namespace['apply']
out=namespace['OUT']/'PosesFreshV6';out.mkdir(exist_ok=True)
scene.render.engine='BLENDER_WORKBENCH'
scene.render.resolution_x=480;scene.render.resolution_y=480;scene.render.resolution_percentage=100
scene.display.shading.light='STUDIO';scene.display.shading.studio_light='paint.sl'
scene.camera.data.ortho_scale=2.7
poses=[('Idle',namespace['IDLE']),('Fire_Release',namespace['THROW']),('Summon_Low',namespace['SUM_LOW']),
    ('Rain_Channel',namespace['rain_loop'](0)),('Teleport_Fold',namespace['TELE_FOLD']),('Death_Final',namespace['DEAD'])]
for label,p in poses:
    apply(p)
    scene.render.filepath=str(out/(label+'.png'))
    bpy.ops.render.render(write_still=True)
    print('FRESH_V6_POSE',label,flush=True)
