"""Render actual mesh motion at native default timing without modifying authored blend.

blender --background --factory-startup --python Tools/Blender/preview_nut_tank_style.py
Saved frame cache is disposable. MP4 and contact sheet remain with final source.
"""
import hashlib,json,math,subprocess
from pathlib import Path
import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/NutAnimationStyle/Tank'
CACHE=ROOT/'Saved/NutAnimationStyle/TankReview/Frames'
SECTIONS=[('Idle',[(3.2,1,81)]),('Walk',[(1.92,1,49)]),('WalkLeft',[(1.92,1,49)]),('WalkRight',[(1.92,1,49)]),
          ('Melee',[(.65,1,51),(.9,51,101)]),('Jump',[(1.2,1,26),(.8,26,71),(1.7,71,101)]),
          ('Transform',[(1.2,1,101),(.4,101,101),(1.4,101,1)])]

def digest(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def main():
    CACHE.mkdir(parents=True,exist_ok=True)
    source=OUT/'Nut_Tank_MC_Style.blend';before=digest(source)
    bpy.ops.wm.open_mainfile(filepath=str(source));scene=bpy.context.scene;r=bpy.data.objects['rig']
    scene.render.resolution_x=640;scene.render.resolution_y=640
    r.animation_data.use_nla=False
    for track in r.animation_data.nla_tracks:track.mute=True
    index=0;sections=[];fps=25
    for label,segments in SECTIONS:
        action=bpy.data.actions['MC_Tank_'+label];r.animation_data.action=action;r.animation_data.action_slot=action.slots[0]
        first=index
        for seconds,start,end in segments:
            count=round(seconds*fps)
            for i in range(count):
                frame=start+(end-start)*i/max(1,count-1)
                scene.frame_set(math.floor(frame),subframe=frame-math.floor(frame))
                scene.render.filepath=str(CACHE/f'{index:05}.png');bpy.ops.render.render(write_still=True);index+=1
        sections.append({'label':label,'start_index':first,'end_index':index-1,'seconds':(index-first)/fps,'timing_segments':segments})
        print('TANK_REVIEW_SECTION',label,index-first,flush=True)
    # Full foot-control sweep catches hidden seams and floor drift, plus equipment motion.
    validation={}
    for label in ('Idle','Walk','WalkLeft','WalkRight','Melee','Jump','Transform'):
        action=bpy.data.actions['MC_Tank_'+label];r.animation_data.action=action;r.animation_data.action_slot=action.slots[0]
        points=[]
        for frame in range(1,int(action.frame_end)+1):
            scene.frame_set(frame);bpy.context.view_layer.update();ev=r.evaluated_get(bpy.context.evaluated_depsgraph_get())
            points.append({n:(ev.matrix_world@ev.pose.bones[n].matrix).translation.copy() for n in ('c_pos','foot.l','foot.r','hand.l','hand.r','root.x')})
        item={'root_control_max_displacement_m':max((p['c_pos']-points[0]['c_pos']).length for p in points)}
        if label in ('Idle','Walk','WalkLeft','WalkRight'):
            item['loop_position_error_m']=max((points[0][n]-points[-1][n]).length for n in points[0])
        if label in ('Idle','Melee'):
            item['foot_max_displacement_m']=max((p[n]-points[0][n]).length for p in points for n in ('foot.l','foot.r'))
        if label=='Jump':
            item['takeoff_frame']=26;item['landing_frame']=71
            item['landing_to_idle_pose_error_m']=max((points[70][n]-points[0][n]).length for n in points[0])
        if label=='Transform':item['terminal_hold_error_m']=max((p[n]-points[69][n]).length for p in points[69:] for n in points[0])
        validation[label]=item
    report={'fps':fps,'sections':sections,'frames':index,'source_blend_sha256_before':before,'source_blend_sha256_after':digest(source),
            'animation_validation':validation,'native_default_timing_preview':True,'material_scope':'Workbench uses original geometry and source material display; no mesh/material edits.',
            'limitations':'Preview reproduces default state clip timing only. Unreal airborne overrides and ball substitution are not simulated.'}
    (OUT/'PreviewReport.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    # ffmpeg does not modify Blender files. Quiet output keeps task logs legible.
    filter_parts=[]
    for s in sections:
        text=s['label']+'  |  MC Tank style v1'
        lo=s['start_index']/fps;hi=(s['end_index']+1)/fps
        filter_parts.append(f"drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='{text}':x=24:y=26:fontsize=22:fontcolor=white:box=1:boxcolor=black@0.45:boxborderw=9:enable='gte(t,{lo})*lt(t,{hi})'")
    command=['C:/ffmpeg/ffmpeg.exe','-hide_banner','-loglevel','error','-y','-framerate',str(fps),'-start_number','0','-i',str(CACHE/'%05d.png'),'-frames:v',str(index),'-vf',','.join(filter_parts),'-c:v','libx264','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart',str(OUT/'Tank_Style_Review.mp4')]
    subprocess.run(command,check=True)
    print('TANK_REVIEW_PASS',index,flush=True)

if __name__=='__main__':main()
