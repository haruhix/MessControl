"""Root visual repair after native frame03; scoped to the owned menu map/materials."""
from pathlib import Path
import hashlib,json,shutil
import unreal as u
OUT=Path(globals().get('MC_MENU_ART_OUT','E:/CodexWork/MessControlMenuShowcase_20261021/RootArtRepair02'))
assert OUT.drive.upper()=='E:' and not OUT.exists()
OUT.mkdir(parents=True)
es=u.get_editor_subsystem(u.EditorActorSubsystem)
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
assert not list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
owned={a.get_actor_label():a for a in es.get_all_level_actors()}
assert all('MC_MenuShowcase_OwnedV1' in [str(t) for t in a.tags] for a in owned.values())
root=Path('E:/DEVGAME/MessControl')
packages=['Content/Maps/L_MainMenu.umap','Content/UI/MenuShowcase/Materials/A/M_MenuTongue.uasset','Content/UI/MenuShowcase/Materials/A/M_MenuEnamelCoffee.uasset','Content/UI/MenuShowcase/Materials/A/M_MenuEnamelClean.uasset']
backups=[]
for relative in packages:
    source=root/relative; target=OUT/'Before'/relative
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    assert hashlib.sha256(source.read_bytes()).digest()==hashlib.sha256(target.read_bytes()).digest()
    backups.append(str(target))
edit=u.MaterialEditingLibrary
tongue=u.load_asset('/Game/UI/MenuShowcase/Materials/A/M_MenuTongue')
coffee=u.load_asset('/Game/UI/MenuShowcase/Materials/A/M_MenuEnamelCoffee')
for m in (tongue,coffee):
    assert u.EditorAssetLibrary.get_metadata_tag(m,'MC_MenuShowcaseStage')=='menu_showcase_static_v1'
    m.modify()
for n in edit.get_material_expressions(tongue):
    desc=str(n.get_editor_property('desc'))
    if desc=='MC_MenuTonguePatternAmount':n.set_editor_property('r',.60)
    if desc=='MC_MenuTongueNormalAmount':n.set_editor_property('r',.65)
    if desc=='MC_MenuTongueRoughness':n.set_editor_property('r',.43)
    if desc=='MC_MenuTongueSpecular':n.set_editor_property('r',.35)
code=r'''
float3 d=P-Contact.xyz;
float2 q=float2(dot(d,TangentU.xyz),dot(d,TangentV.xyz));
float3 centerOffset=TangentV.xyz*12.+TangentU.xyz*4.;
float r=length(d-centerOffset);
float grain=sin(q.x*.17+sin(q.y*.14)*1.1)*2.2+sin(q.y*.31+q.x*.07)*1.5+sin(q.x*.53-q.y*.37)*.7;
float coverage=1.-smoothstep(29.+grain,31.+grain,r);
float2 a=float2(-5.,-17.),b=float2(0.,0.);
float2 pa=q-a,ba=b-a;
float t=saturate(dot(pa,ba)/dot(ba,ba));
float stroke=1.-smoothstep(4.0,5.5,length(pa-ba*t));
float mottling=.90;
return saturate(coverage*mottling*(1.-stroke*.82));
'''
customs=[n for n in edit.get_material_expressions(coffee) if isinstance(n,u.MaterialExpressionCustom) and str(n.get_editor_property('description'))=='MC_MenuShowcase_CoffeeMask_v1']
assert len(customs)==1
customs[0].set_editor_property('code',code)
errors={m.get_name():[str(e) for e in edit.recompile_material(m)] for m in (tongue,coffee)}
assert all(not v for v in errors.values()),str(errors)
clean=u.load_asset('/Game/UI/MenuShowcase/Materials/A/M_MenuEnamelClean')
assert u.EditorAssetLibrary.get_metadata_tag(clean,'MC_MenuShowcaseStage')=='menu_showcase_static_v1'
for m in (clean,coffee):
    nodes=list(edit.get_material_expressions(m))
    n=next((n for n in nodes if str(n.get_editor_property('desc'))=='MC_MenuRootControlledSpecular'),None)
    if n is None:
        n=edit.create_material_expression(m,u.MaterialExpressionConstant)
        n.set_editor_property('desc','MC_MenuRootControlledSpecular')
    n.set_editor_property('r',.18)
    assert edit.connect_material_property(n,'',u.MaterialProperty.MP_SPECULAR)
    e=[str(x) for x in edit.recompile_material(m)]
    assert not e,str(e)
for m in (tongue,coffee,clean):assert u.EditorAssetLibrary.save_loaded_asset(m,only_if_is_dirty=False)
with u.ScopedEditorTransaction('Root visual correction of menu materials and soft light'):
    rect=owned['MC_MenuShowcase_WarmKey'].get_component_by_class(u.RectLightComponent)
    rect.set_intensity(0.)
    rect.set_cast_shadows(False)
    old=owned.get('MC_MenuShowcase_SoftShadowKey')
    if old:assert es.destroy_actor(old)
    key=es.spawn_actor_from_class(u.PointLight,u.Vector(0,240,280))
    key.set_actor_label('MC_MenuShowcase_SoftShadowKey')
    key.tags=[u.Name('MC_MenuShowcase_OwnedV1'),u.Name('MC_MenuShowcase_SoftShadowKey')]
    key.set_folder_path('MenuShowcase/A')
    light=key.get_component_by_class(u.PointLightComponent)
    light.set_mobility(u.ComponentMobility.MOVABLE)
    light.set_intensity_units(u.LightUnits.CANDELAS)
    light.set_intensity(100.)
    light.set_light_color(u.LinearColor(1.,.88,.77,1.))
    light.set_attenuation_radius(1800.)
    light.set_editor_property('source_radius',0.)
    light.set_editor_property('soft_source_radius',0.)
    light.set_cast_shadows(True)
    light.set_editor_property('shadow_bias',.20)
    light.set_editor_property('shadow_slope_bias',.35)
    light.set_editor_property('contact_shadow_length',0.)
    palate=owned['MC_MenuShowcase_SculptedPalate']
    palate.set_actor_rotation(u.Rotator(pitch=0.,yaw=-90.,roll=0.),False)
    palate.set_actor_location(u.Vector(40,-750,-17.5),False,True)
    palate.static_mesh_component.set_cast_shadow(False)
    uvula=owned['MC_MenuShowcase_ExistingUvula']
    uvula.set_actor_location(u.Vector(0,-630,205.5),False,True)
    uvula.set_actor_hidden_in_game(False)
    uvula.set_is_temporarily_hidden_in_editor(False)
    uvula.static_mesh_component.set_cast_shadow(False)
    owned['MC_MenuShowcase_Brush'].static_mesh_component.set_cast_shadow(False)
    camera=owned['MC_MenuShowcase_Camera'].get_component_by_class(u.CameraComponent)
    pp=camera.get_editor_property('post_process_settings')
    for name,value in {'depth_of_field_focal_distance':650.,'depth_of_field_fstop':1.4}.items():
        pp.set_editor_property('override_'+name,True)
        pp.set_editor_property(name,value)
    camera.set_editor_property('post_process_settings',pp)
    cube=u.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap')
    if isinstance(cube,u.TextureCube):
        sky=owned.get('MC_MenuShowcase_Ambient')
        if sky:assert es.destroy_actor(sky)
        sky=es.spawn_actor_from_class(u.SkyLight,u.Vector(0,0,300))
        sky.set_actor_label('MC_MenuShowcase_Ambient')
        sky.tags=[u.Name('MC_MenuShowcase_OwnedV1'),u.Name('MC_MenuShowcase_Ambient')]
        sky.set_folder_path('MenuShowcase/A')
        sk=sky.get_component_by_class(u.SkyLightComponent)
        sk.set_mobility(u.ComponentMobility.MOVABLE)
        sk.set_editor_property('source_type',u.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        sk.set_cubemap(cube)
        sk.set_intensity(.35)
        sk.set_cast_shadows(False)
    assert ls.save_current_level()
RESULT={'status':'pending_root_visual_review','backups':backups,'shader_errors':errors,'ambient_cubemap_found':isinstance(cube,u.TextureCube),'changes':['Actual tongue texture contrast and fine normals','Small irregular coffee patch centered at brush contact with short clean stroke','Controlled specular and disabled brush shadow; ordinary project shadow maps retained','Source-confirmed palate orientation and joined uvula root'],'no_gameplay_changes':True}
(OUT/'Report.json').write_text(json.dumps(RESULT,indent=2),encoding='utf8')
