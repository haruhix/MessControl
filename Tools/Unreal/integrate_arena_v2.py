"""Connect the September 30 artist arena to gameplay. Run in the editor, PIE stopped."""
import json
from pathlib import Path
import unreal as u

lib=u.EditorAssetLibrary
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
edit=u.MaterialEditingLibrary
tag='ArtistArenaIntegrationV2'
all_actors=actors.get_all_level_actors()
assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE first'
folder='/Game/Gameplay/Arena/V2'
lib.make_directory(folder)
teeth_material=lib.load_asset('/Game/Art/Materials/Arena/MI_Teeth_v2')
gum_material=lib.load_asset('/Game/Art/Materials/Arena/MI_Gum_v2')
wall_material=lib.load_asset('/Game/Art/Materials/MI_MouthCheek')
assert teeth_material and gum_material and wall_material

def save(a):
    assert lib.save_loaded_asset(a,only_if_is_dirty=False), a.get_path_name()

def collision(mesh):
    body=mesh.get_editor_property('body_setup')
    body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    body.set_editor_property('double_sided_geometry',True)
    save(mesh)

# Keep the existing replicated contact/wipe shader, using the artist's new UV textures.
path=folder+'/M_ArenaToothCareV2'
material=lib.load_asset(path) if lib.does_asset_exist(path) else lib.duplicate_asset('/Game/Gameplay/Care/M_ArenaToothCare',path)
nodes=edit.get_material_expressions(material)
color=next(n for n in nodes if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')=='MC Care: cleaningColor')
rough=next(n for n in nodes if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')=='MC Care: cleaningRoughness')
normal=next(n for n in nodes if isinstance(n,u.MaterialExpressionCustom) and n.get_editor_property('description')=='MC Care: filmNormal')
def texture(name,parameter,sampler,y):
    node=next((n for n in nodes if isinstance(n,u.MaterialExpressionTextureSampleParameter2D) and str(n.get_editor_property('parameter_name'))==parameter),None)
    if not node: node=edit.create_material_expression(material,u.MaterialExpressionTextureSampleParameter2D,-600,y)
    node.set_editor_property('parameter_name',parameter)
    node.set_editor_property('texture',lib.load_asset('/Game/Art/Texture/Arena/teeth_loc_low_DefaultMaterial_'+name))
    node.set_editor_property('sampler_type',sampler)
    return node
albedo=texture('BaseColor','Albedo Texture',u.MaterialSamplerType.SAMPLERTYPE_COLOR,2200)
arm=texture('OcclusionRoughnessMetallic','ARM',u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,2400)
norm=texture('Normal','Normal Texture',u.MaterialSamplerType.SAMPLERTYPE_NORMAL,2600)
assert edit.connect_material_expressions(albedo,'RGB',color,'Enamel')
assert edit.connect_material_expressions(arm,'G',rough,'Base')
transform=next((n for n in nodes if isinstance(n,u.MaterialExpressionTransform) and n.get_editor_property('desc')=='Arena V2 normal to world'),None)
if not transform: transform=edit.create_material_expression(material,u.MaterialExpressionTransform,-300,2600)
transform.set_editor_property('desc','Arena V2 normal to world')
transform.set_editor_property('transform_source_type',u.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_TANGENT)
transform.set_editor_property('transform_type',u.MaterialVectorCoordTransform.TRANSFORM_WORLD)
assert edit.connect_material_expressions(norm,'RGB',transform,'')
assert edit.connect_material_expressions(transform,'',normal,'N')
errors=edit.recompile_material(material)
assert not errors, str(errors)
save(material)
profile=lib.load_asset('/Game/Data/DA_ArenaTooth')
profile.set_editor_property('gameplay_material',material)
save(profile)

existing=[a for a in all_actors if isinstance(a,u.MCArenaToothSocket) and tag in [str(t) for t in a.tags]]
layout=[]
if existing:
    assert sorted(a.tooth_id for a in existing)==list(range(1,9)), 'Inspect incomplete migration'
else:
    sources=[]
    for side in ('L','R'):
        for index in range(1,5):
            name='SM_Teeth_'+('l' if side=='L' and index==4 else side)+'_%02d'%index
            matches=[a for a in all_actors if isinstance(a,u.StaticMeshActor) and a.static_mesh_component.static_mesh and a.static_mesh_component.static_mesh.get_path_name()=='/Game/FromBlender2/'+name+'.'+name]
            assert len(matches)==1, (name,len(matches))
            sources.append((index*2-1 if side=='L' else index*2,matches[0]))
    for number,source in sources:
        c=source.static_mesh_component
        mesh_path=folder+'/'+c.static_mesh.get_name()
        mesh=lib.load_asset(mesh_path) if lib.does_asset_exist(mesh_path) else lib.duplicate_asset(c.static_mesh.get_path_name(),mesh_path)
        collision(mesh)
        marker=actors.spawn_actor_from_class(u.MCArenaToothSocket,c.get_world_location(),c.get_world_rotation())
        marker.set_actor_scale3d(c.get_world_scale())
        marker.set_editor_property('tooth_id',number)
        marker.preview.set_static_mesh(mesh)
        marker.preview.set_material(0,material)
        marker.set_actor_label('ARENA | Tooth %02d'%number)
        marker.set_folder_path('Gameplay/ArenaTeeth')
        marker.set_editor_property('tags',[tag])
        p=c.get_world_location()
        layout.append(dict(id=number,mesh=mesh.get_path_name(),source=source.get_actor_label(),position=[p.x,p.y,p.z]))
        actors.destroy_actor(source)

bins=[a for a in all_actors if isinstance(a,u.MCFoodDisposal) and a.get_editor_property('brush_bin')]
if not bins:
    bin_actor=actors.spawn_actor_from_class(u.MCFoodDisposal,u.Vector(-1110,0,140),u.Rotator())
    bin_actor.set_editor_property('brush_bin',True)
    bin_actor.get_editor_property('volume').set_box_extent(u.Vector(70,680,240))
    bin_actor.get_editor_property('label').set_relative_location(u.Vector(80,0,0))
    bin_actor.set_actor_label('GAMEPLAY | Brush disposal')
    bin_actor.set_folder_path('Gameplay')

for a in actors.get_all_level_actors():
    if isinstance(a,u.MCFoodDisposal) and a.get_editor_property('brush_bin'):
        # The HUD supplies the tool hint. A label at the near camera boundary
        # otherwise fills the viewport when the following camera approaches it.
        a.get_editor_property('label').set_hidden_in_game(True)
        a.get_editor_property('label').set_visibility(False)
    if isinstance(a,u.SkeletalMeshActor):
        c=a.skeletal_mesh_component
        if c.skeletal_mesh_asset and c.skeletal_mesh_asset.get_path_name()=='/Game/FromBlender2/SK_Exit.SK_Exit':
            # Retain the imported source actor for art work. The authored,
            # functional living throat and uvula supply this part in play.
            a.set_actor_hidden_in_game(True)
            c.set_visibility(False)
            c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    if not isinstance(a,u.StaticMeshActor) or not a.static_mesh_component.static_mesh: continue
    c=a.static_mesh_component
    path=c.static_mesh.get_path_name()
    if path.startswith('/Game/FromBlender2/'):
        c.set_material(0,gum_material if c.static_mesh.get_name()=='SM_Gum' else wall_material if c.static_mesh.get_name()=='SM_Roof_Wall' else teeth_material)
        collision(c.static_mesh)
    if a.get_actor_label()=='COLLISION | Artist mouth':
        c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    if a.get_actor_label().startswith('COLLISION |'):
        a.set_actor_hidden_in_game(True)
        c.set_editor_property('use_default_collision',False)
        c.set_collision_response_to_channel(u.CollisionChannel.ECC_CAMERA,u.CollisionResponseType.ECR_IGNORE)

throats=[a for a in actors.get_all_level_actors() if isinstance(a,u.MCThroat)]
assert len(throats)<=1, 'Inspect duplicate living throats'
throat=throats[0] if throats else actors.spawn_actor_from_class(u.MCThroat,u.Vector(1300,-25,-40),u.Rotator())
throat.set_actor_label('GAMEPLAY | Living throat')
throat.set_folder_path('Gameplay/Throat')
throat.set_editor_property('tags',[tag])
throat.set_editor_property('tissue_material',lib.load_asset('/Game/Art/Materials/MI_LivingThroat'))
throat.set_editor_property('ring_material',lib.load_asset('/Game/Art/Materials/MI_ThroatRing'))
throat.get_editor_property('uvula').set_static_mesh(lib.load_asset('/Game/Gameplay/Throat/SM_Uvula'))
throat.get_editor_property('uvula').set_material(0,lib.load_asset('/Game/Art/Materials/MI_MouthPalate'))
throat.get_editor_property('sculpted_tissue').set_skeletal_mesh_asset(lib.load_asset('/Game/Gameplay/Throat/SK_Throat'))
throat.call_method('RebuildAppearance')

bound=next((a for a in all_actors if 'MCCameraBounds' in [str(t) for t in a.tags]),None)
if not bound:
    bound=actors.spawn_actor_from_class(u.TriggerBox,u.Vector(-350,-25,350),u.Rotator())
    bound.set_actor_label('CAMERA | Follow bounds')
    bound.set_folder_path('Gameplay/Camera')
    bound.set_editor_property('tags',['MCCameraBounds',tag])
    box=bound.get_component_by_class(u.BoxComponent)
    box.set_box_extent(u.Vector(1000,640,300))
    box.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    box.set_editor_property('line_thickness',4)
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
if not layout:
    for a in actors.get_all_level_actors():
        if isinstance(a,u.MCArenaToothSocket) and tag in [str(t) for t in a.tags]:
            p=a.get_actor_location()
            layout.append(dict(id=a.tooth_id,mesh=a.preview.static_mesh.get_path_name(),position=[p.x,p.y,p.z]))
Path(u.Paths.project_saved_dir(),'ArenaV2Integration.json').write_text(json.dumps(dict(layout=sorted(layout,key=lambda r:r['id']),bounds=bound.get_path_name(),throat=throat.get_path_name(),uvula=throat.get_editor_property('uvula').static_mesh.get_path_name()),indent=2),encoding='utf-8')
u.log('MC_ARENA_V2_INTEGRATED: 8 gameplay teeth, bounded following camera and original functional uvula')
