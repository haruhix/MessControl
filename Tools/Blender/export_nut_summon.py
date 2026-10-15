"""Bake the supplied Summon.blend mesh/run, without altering the artist source."""
import bpy,addon_utils,sys,json
from pathlib import Path
from mathutils import Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from export_nut_tank import SETTINGS,digest,fbx_inventory,select
ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path('C:/Users/vasil/Downloads/Telegram Desktop/Summon.blend')
OUT=ROOT/'ArtSource/NutSummon'

def main():
 OUT.mkdir(parents=True,exist_ok=True);original=digest(SOURCE)
 version=f'{bpy.app.version[0]}.{bpy.app.version[1]}'
 sys.path.insert(0,str(Path.home()/f'AppData/Roaming/Blender Foundation/Blender/{version}/extensions/user_default'))
 assert addon_utils.enable('auto_rig_pro',default_set=True,persistent=False)
 bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
 scene=bpy.context.scene;rig=bpy.data.objects['rig'];mesh=bpy.data.objects['SM_NutWizard']
 assert any(m.type=='ARMATURE' and m.object==rig for m in mesh.modifiers)
 rig.animation_data_create();rig.animation_data.action=None;rig.animation_data.use_nla=False
 for t in rig.animation_data.nla_tracks:t.mute=True
 for b in rig.pose.bones:b.matrix_basis=Matrix.Identity(4)
 for k,v in SETTINGS.items():setattr(scene,k,v)
 report={'status':'exporting','source':str(SOURCE),'source_sha256_before':original,'fps':scene.render.fps/scene.render.fps_base,'blender':bpy.app.version_string,'files':[],'poses':[],'textures':[]}
 def save():(OUT/'ExportReport.json').write_text(json.dumps(report,indent=2))
 def export(name):
  path=OUT/name;assert not path.exists(),path
  select([rig,mesh],rig);assert 'FINISHED' in bpy.ops.arp.arp_export_fbx_panel(filepath=str(path))
  info=fbx_inventory(path);report['files'].append({'file':name,'sha256':digest(path),'fbx':info});save();return info
 scene.arp_bake_anim=False;scene.frame_set(1)
 ref=export('SK_NutSummon.fbx')
 action=bpy.data.actions['Summon_walk'];rig.animation_data.action=action;rig.animation_data.action_slot=next(s for s in action.slots if s.target_id_type=='OBJECT')
 start,end=map(round,action.frame_range);scene.arp_bake_anim=True;scene.arp_frame_range_type='CUSTOM'
 scene.frame_start=start;scene.frame_end=end;scene.arp_export_start_frame=start;scene.arp_export_end_frame=end
 for frame in range(start,end+1):
  scene.frame_set(frame);ev=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
  report['poses'].append({'frame':frame,'points':{n:list((ev.matrix_world@ev.pose.bones[n].matrix).translation) for n in ['root.x','foot.l','foot.r','hand.l','hand.r']}})
 scene.frame_set(start);info=export('A_NutSummon_Run.fbx')
 assert info['hierarchy_sha256']==ref['hierarchy_sha256'] and info['root_animation_constant']
 binds={p['name']:p['matrix'] for p in ref['bind_poses']};current={p['name']:p['matrix'] for p in info['bind_poses']}
 assert set(binds)==set(current) and max(abs(a-b) for n in binds for a,b in zip(binds[n],current[n]))<.0001
 textures=OUT/'Textures';textures.mkdir(exist_ok=True)
 for name,file in [('hand_eye','T_Summon_HandEye.png'),('stylized_walnut_game_ready_texture_0.PNG','T_Summon_Walnut.png')]:
  image=bpy.data.images[name];path=textures/file;assert not path.exists(),path
  image.filepath_raw=str(path);image.file_format='PNG';image.save()
  report['textures'].append({'source_image':name,'file':'Textures/'+file,'sha256':digest(path)})
 report.update(status='complete',source_action=action.name,start_frame=start,end_frame=end,duration_seconds=(end-start)/report['fps'],source_sha256_after=digest(SOURCE),source_preserved=digest(SOURCE)==original)
 assert report['source_preserved'];save();print('SUMMON_EXPORT_PASS',json.dumps({k:v for k,v in report.items() if k not in ['files','poses']}),flush=True)
if __name__=='__main__':main()
