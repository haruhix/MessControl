"""Own menu coffee film and restrained projected contact occlusion.

The contact decal follows Epic's DBuffer decal material/actor workflow. No
gameplay assets, collision, shadow CVars or shared materials are modified.
"""
import unreal as u
from pathlib import Path
BASE='/Game/UI/MenuShowcase/AnatomyV2'
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem)
assert not ls.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name().startswith(BASE+'/L_MenuAnatomyV2.')
lib=u.EditorAssetLibrary;edit=u.MaterialEditingLibrary
coffee=lib.load_asset(BASE+'/Materials/M_MenuCoffeeEnamel')
nodes={str(n.get_editor_property('description')):n for n in edit.get_material_expressions(coffee) if isinstance(n,u.MaterialExpressionCustom)}
nodes['One connected feathered coffee film at bristle contact'].set_editor_property('code','''
float3 d=P-C;
float2 q=float2(dot(d,U)/58.0,dot(d,V)/66.0);
float a=atan2(q.y,q.x);
float edge=.87+.11*sin(a*3.0+.5)+.055*sin(a*7.0-.8)+.035*sin(a*11.0);
float boundary=1.0-smoothstep(edge-.13,edge+.065,length(q));
float slab=1.0-smoothstep(28.0,58.0,abs(dot(d,W)));
float facing=smoothstep(.03,.35,dot(N,W));
return saturate(boundary*slab*facing);
''')
nodes['Espresso concentrate and translucent warm edges'].set_editor_property('code','''
float3 pigment=lerp(float3(.175,.070,.024),float3(.055,.014,.006),N*.8);
float density=saturate(Mask*(.78+.20*N));
return lerp(Enamel,pigment,density);
''')
nodes['Thin wet coffee film retains enamel underneath'].set_editor_property('code','return lerp(Rough,.17+N*.075,Mask*.86);')
assert not list(edit.recompile_material(coffee));assert lib.save_loaded_asset(coffee)
path=BASE+'/Materials/M_MenuFootContact'
m=lib.load_asset(path)
if not m:m=u.AssetToolsHelpers.get_asset_tools().create_asset('M_MenuFootContact',BASE+'/Materials',u.Material,u.MaterialFactoryNew())
assert isinstance(m,u.Material)
edit.delete_all_material_expressions(m)
m.set_editor_property('material_domain',u.MaterialDomain.MD_DEFERRED_DECAL);m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
uv=edit.create_material_expression(m,u.MaterialExpressionTextureCoordinate)
mask=edit.create_material_expression(m,u.MaterialExpressionCustom);mask.set_editor_property('description','Soft sole contact, no hard directional toy shadow')
mask.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT1)
inp=u.CustomInput();inp.set_editor_property('input_name','UV');mask.set_editor_property('inputs',[inp])
mask.set_editor_property('code','float2 p=(UV-.5)*2.0; float r=length(p); return .28*exp(-3.8*dot(p,p))*(1.0-smoothstep(.70,1.0,r));')
assert edit.connect_material_expressions(uv,'',mask,'UV')
color=edit.create_material_expression(m,u.MaterialExpressionConstant3Vector);color.set_editor_property('constant',u.LinearColor(.026,.010,.016,1))
assert edit.connect_material_property(color,'',u.MaterialProperty.MP_BASE_COLOR)
assert edit.connect_material_property(mask,'',u.MaterialProperty.MP_OPACITY)
assert not list(edit.recompile_material(m));assert lib.save_loaded_asset(m)
actors=es.get_all_level_actors();a=next((a for a in actors if a.get_actor_label()=='MENU V2 | Soft sole contact'),None)
if not a:a=es.spawn_actor_from_class(u.DecalActor,u.Vector(-620,315,20));a.set_actor_label('MENU V2 | Soft sole contact');a.tags=['MC_MenuV2_Owned']
a.set_actor_location(u.Vector(-620,315,20),False,True);a.set_actor_rotation(u.Rotator(pitch=-90),False)
c=a.get_component_by_class(u.DecalComponent);c.set_decal_material(m);c.set_editor_property('decal_size',u.Vector(30,50,38));c.set_editor_property('fade_screen_size',.0001)
hero=next(a for a in actors if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);hero.skeletal_mesh_component.set_editor_property('receives_decals',False)
assert ls.save_current_level();u.log('MENU_CONTACT_FILM_AND_SOLE_SAVED')
