"""Inspect and render the distinct Charge and actual full Ball-Roll review scene.

Blender --background --factory-startup --python this.py. Source blend remains untouched.
"""
import hashlib,json,subprocess
from pathlib import Path
import bpy

ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'ArtSource/NutAnimationStyle/Tank';CACHE=ROOT/'Saved/NutAnimationStyle/TankV2Review/Frames'

def digest(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def main():
    CACHE.mkdir(parents=True,exist_ok=True);source=OUT/'Nut_Tank_MC_Style_v2.blend';before=digest(source)
    report=json.loads((OUT/'Tank_Abilities_v2_Report.json').read_text())
    bpy.ops.wm.open_mainfile(filepath=str(source));review=bpy.data.scenes[report['review']['scene']]
    if bpy.context.window:bpy.context.window.scene=review
    ball=bpy.data.objects['MCV2_RollBall'];rig=bpy.data.objects['MCV2_ReviewRig'];course=bpy.data.objects['MCV2_PresentationMotion']
    fps=review.render.fps;key_samples={};selected={1:'ChargeTell_Start',18:'ChargeTell_Coil',31:'ChargeLoop',77:'ChargeRecovery_Stop',104:'ChargeRecovery_Guard'}
    roll_start=next(s['start_seconds'] for s in report['review']['sections'] if s['label'].startswith('RollTransform'))
    for suffix,seconds in [('RollTransform_Coil',roll_start+.7),('RollTransform_Swap',roll_start+1.15),('Rolling_Ball',roll_start+1.7),('Rolling_Turn',roll_start+3),('Rolling_ContactProxy',roll_start+3.1),('Rolling_Ricochet',roll_start+4.5),('Unfold_Swap',roll_start+6.25),('Unfold_Guard',roll_start+7.5)]:selected[round(seconds*fps)+1]=suffix
    for f in range(review.frame_start,review.frame_end+1):
        review.frame_set(f);bpy.context.view_layer.update();review.render.filepath=str(CACHE/f'{f-1:05}.png');bpy.ops.render.render(write_still=True,scene=review.name)
        if f in selected:
            tag=selected[f];review.render.filepath=str(OUT/f'Tank_v2_{tag}.png');bpy.ops.render.render(write_still=True,scene=review.name)
            key_samples[tag]={'frame':f,'seconds':(f-1)/fps,'ball_visible':not ball.hide_render,'ball_scale':list(ball.scale),'course_location':list(course.location),'course_heading_degrees':float(course.rotation_euler.z)*180/3.141592653589793}
        if f%50==0:print('TANK_V2_PREVIEW_PROGRESS',f,review.frame_end,flush=True)
    filters=[]
    for s in report['review']['sections']:
        label=s['label'].replace(',',' /');a=s['start_seconds'];b=s['end_seconds']
        filters.append(f"drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='{label}':x=20:y=22:fontsize=21:fontcolor=white:box=1:boxcolor=black@0.6:boxborderw=8:enable='gte(t,{a})*lt(t,{b})'")
    filters.append("drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='Presentation review | original ball mesh | collision / physics not simulated':x=20:y=600:fontsize=15:fontcolor=white:box=1:boxcolor=black@0.55:boxborderw=6")
    subprocess.run(['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','error','-y','-framerate',str(fps),'-start_number','0','-i',str(CACHE/'%05d.png'),'-frames:v',str(review.frame_end),'-vf',','.join(filters),'-c:v','libx264','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(OUT/'Tank_Abilities_v2_Review.mp4')],check=True)
    subprocess.run(['C:/Python314/python.exe',str(Path(__file__).with_name('compose_nut_tank_contact_sheet.py'))],check=True)
    qa={'status':'rendered_review_pass','source_blend_sha256_before':before,'source_blend_sha256_after':digest(source),'review_scene':review.name,'frames':review.frame_end,'duration_seconds':review.frame_end/fps,'fps':fps,'key_samples':key_samples,
        'new_actions_present':all(n in bpy.data.actions for n in ['MC_Tank_ChargeTell','MC_Tank_ChargeLoop','MC_Tank_ChargeRecovery']),
        'ball_mesh_vertices':len(ball.data.vertices),'original_ball_mesh_vertices':len(bpy.data.objects['stylized_walnut_game_ready.001'].data.vertices),
        'review_uses_actual_saved_ball_mesh':len(ball.data.vertices)==len(bpy.data.objects['stylized_walnut_game_ready.001'].data.vertices),
        'scope':'Blender visual presentation only. Runtime slots, export/import, gameplay contacts, push physics and networking are pending.'}
    (OUT/'Tank_Abilities_v2_PreviewReport.json').write_text(json.dumps(qa,indent=2),encoding='utf8')
    print('TANK_V2_PREVIEW_PASS',json.dumps({'frames':review.frame_end,'duration':review.frame_end/fps,'ball':len(ball.data.vertices)}),flush=True)

if __name__=='__main__':main()
