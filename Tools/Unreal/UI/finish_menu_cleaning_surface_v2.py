"""Menu-only coffee patch and existing gameplay foam, no gameplay assets edited."""
import unreal as u,json
from pathlib import Path
es=u.get_editor_subsystem(u.EditorActorSubsystem);lib=u.EditorAssetLibrary;e=u.MaterialEditingLibrary
BASE='/Game/UI/MenuShowcase/AnatomyV2';world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith(BASE+'/L_MenuAnatomyV2.')
tooth=next(a for a in es.get_all_level_actors() if 'Tooth 04 |' in a.get_actor_label())
brush=next(a for a in es.get_all_level_actors() if 'MC_MenuV2_Brush' in [str(t) for t in a.tags])
# Sequencer restores its editor pre-animated actor transform on map load. Use
# the authored frame, not that transient editor pose, for the shader's contact.
sample=json.loads(Path('E:/DEVGAME/MessControl/ArtSource/UI/MenuSceneV2_20261022/Blender/Exports/A_Menu_CleanTooth_Loop_V2_BrushWorld.json').read_text())['samples'][20]
hero=next(a for a in es.get_all_level_actors() if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);ht=hero.get_actor_transform()
loc=sample['location_m'];q=sample['quaternion_wxyz'];pos=ht.transform_location(u.Vector(loc[0]*100,-loc[1]*100,loc[2]*100))
rotation=(ht.rotation*u.Quat(-q[1],q[2],-q[3],q[0])).rotator()
pose=u.Transform();pose.set_editor_property('translation',pos);pose.set_editor_property('rotation',rotation.quaternion());pose.set_editor_property('scale3d',ht.scale3d*(sample['scale'][0]/.01))
center=pose.transform_location(u.Vector(80,0,-14))
axis_u=u.MathLibrary.get_right_vector(rotation);axis_v=u.MathLibrary.get_forward_vector(rotation);axis_n=u.MathLibrary.get_up_vector(rotation)
dest=BASE+'/Materials/M_MenuCoffeeEnamel';m=lib.load_asset(dest)
if not m:m=u.AssetToolsHelpers.get_asset_tools().create_asset('M_MenuCoffeeEnamel',BASE+'/Materials',u.Material,u.MaterialFactoryNew())
for n in list(e.get_material_expressions(m)):e.delete_material_expression(m,n)
def node(cls,**kwargs):
 n=e.create_material_expression(m,cls)
 for k,v in kwargs.items():n.set_editor_property(k,v)
 return n
def vec(v):return node(u.MaterialExpressionConstant3Vector,constant=u.LinearColor(v.x,v.y,v.z,1))
pos=node(u.MaterialExpressionWorldPosition)
mask=node(u.MaterialExpressionCustom,description='Soft organic coffee film confined to the contact-facing crown surface',output_type=u.CustomMaterialOutputType.CMOT_FLOAT1,code='float3 d=P-C; float2 q=float2(dot(d,U)/28.0,dot(d,V)/32.0); float a=atan2(q.y,q.x); float edge=0.86+0.07*sin(a*3.0+0.5)+0.035*sin(a*7.0-0.8)+0.025*sin(a*11.0); float boundary=1.0-smoothstep(edge-0.09,edge+0.04,length(q)); float slab=1.0-smoothstep(6.0,18.0,abs(dot(d,W))); float facing=smoothstep(0.10,0.45,dot(N,W)); float film=0.88+0.12*sin(q.x*5.0+sin(q.y*7.0)); return boundary*slab*facing*film;')
inputs=[]
for name in ['P','C','U','V','W','N']:
 inp=u.CustomInput();inp.set_editor_property('input_name',name);inputs.append(inp)
mask.set_editor_property('inputs',inputs)
for name,n in [('P',pos),('C',vec(center)),('U',vec(axis_u)),('V',vec(axis_v)),('W',vec(axis_n)),('N',node(u.MaterialExpressionPixelNormalWS))]:assert e.connect_material_expressions(n,'',mask,name)
enamel=node(u.MaterialExpressionConstant3Vector,constant=u.LinearColor(.63,.60,.52,1));coffee=node(u.MaterialExpressionConstant3Vector,constant=u.LinearColor(.12,.047,.016,1))
mix=node(u.MaterialExpressionLinearInterpolate)
for n,pin in [(enamel,'A'),(coffee,'B'),(mask,'Alpha')]:assert e.connect_material_expressions(n,'',mix,pin)
assert e.connect_material_property(mix,'',u.MaterialProperty.MP_BASE_COLOR)
rough=node(u.MaterialExpressionLinearInterpolate,const_a=.32,const_b=.24);e.connect_material_expressions(mask,'',rough,'Alpha');e.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
spec=node(u.MaterialExpressionConstant,r=.3);e.connect_material_property(spec,'',u.MaterialProperty.MP_SPECULAR)
assert not list(e.recompile_material(m));lib.save_loaded_asset(m);tooth.static_mesh_component.set_material(0,m)
foam=next((a for a in es.get_all_level_actors() if a.get_actor_label()=='MENU V2 | Contact foam'),None)
if not foam:foam=es.spawn_actor_from_class(u.NiagaraActor,u.Vector())
foam.set_actor_label('MENU V2 | Contact foam');foam.tags=['MC_MenuV2_Foam']
c=foam.get_component_by_class(u.NiagaraComponent);c.set_asset(lib.load_asset('/Game/Gameplay/VFX/NS_BrushFoam'))
foam.attach_to_actor(brush,'',u.AttachmentRule.SNAP_TO_TARGET,u.AttachmentRule.SNAP_TO_TARGET,u.AttachmentRule.SNAP_TO_TARGET,False)
foam.set_actor_relative_location(u.Vector(80,0,-10),False,True);foam.set_actor_relative_scale3d(u.Vector(.28,.28,.28));c.set_auto_activate(True);c.activate(True)
world.get_world_settings().set_editor_property('default_game_mode',u.load_class(None,'/Script/MessControl.MCMainMenuGameMode'))
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
u.log('MENU_COFFEE_CONTACT '+str(center))
