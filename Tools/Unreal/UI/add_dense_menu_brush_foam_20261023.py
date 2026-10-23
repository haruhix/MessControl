"""Add dense opaque foam attached to the animated menu brush bristle bone."""
import unreal as u,json,shutil,re
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuDenseBrushFoam20261023';OUT.mkdir(exist_ok=True,parents=True);DEST='/Game/UI/MenuShowcase/DenseBrushFoam20261023'
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem);assert not ls.is_in_play_in_editor();world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world();assert world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
aa=list(es.get_all_level_actors());hero=next(a for a in aa if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);camera=next(a for a in aa if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
assert not any('MC_MenuDenseBrushFoam' in [str(t) for t in a.tags] for a in aa)
backup=OUT/'Before';backup.mkdir(exist_ok=True);shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',backup/'L_MainMenu_Disk.umap');assert ls.save_current_level();shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',backup/'L_MainMenu_ArtistState.umap')
r={'actors_before':{a.get_path_name():str(a.get_actor_transform()) for a in aa},'lighting':{},'postprocess':{},'camera':camera.get_path_name(),'fov':camera.camera_component.field_of_view,'brush_parent':hero.get_path_name(),'bone':'MenuBrush_brush'}
for a in aa:
    if isinstance(a,u.Light):r['lighting'][a.get_path_name()]=u.ToolsetLibrary.get_object_properties(a.light_component,['Intensity','LightColor','Mobility'])
    if isinstance(a,u.PostProcessVolume):r['postprocess'][a.get_path_name()]=str(a.settings)
lib=u.EditorAssetLibrary;edit=u.MaterialEditingLibrary;flag='Interchange.FeatureFlags.Import.FBX';previous=u.SystemLibrary.get_console_variable_int_value(flag)
try:
    u.SystemLibrary.execute_console_command(None,flag+' 0');task=u.AssetImportTask();task.filename=str(ROOT/'ArtSource/UI/MenuDenseBrushFoam20261023/SM_MenuDenseBrushFoam.fbx');task.destination_path=DEST;task.destination_name='SM_MenuDenseBrushFoam';task.automated=True;task.replace_existing=True;task.replace_existing_settings=True;task.save=False;task.factory=u.FbxFactory()
    opt=u.FbxImportUI();opt.automated_import_should_detect_type=False;opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH;opt.import_mesh=True;opt.import_as_skeletal=False;opt.import_materials=False;opt.import_textures=False
    opt.static_mesh_import_data.combine_meshes=True;opt.static_mesh_import_data.auto_generate_collision=False;opt.static_mesh_import_data.set_editor_property('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS);opt.static_mesh_import_data.set_editor_property('vertex_color_import_option',u.VertexColorImportOption.REPLACE);task.options=opt;u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
finally:u.SystemLibrary.execute_console_command(None,flag+' '+str(previous))
mesh=u.load_asset(DEST+'/SM_MenuDenseBrushFoam');assert isinstance(mesh,u.StaticMesh)
export=json.loads((ROOT/'ArtSource/UI/MenuDenseBrushFoam20261023/ExportReport.json').read_text());b=mesh.get_bounds();actual=[[b.origin.x-b.box_extent.x,b.origin.y-b.box_extent.y,b.origin.z-b.box_extent.z],[b.origin.x+b.box_extent.x,b.origin.y+b.box_extent.y,b.origin.z+b.box_extent.z]];assert max(abs(actual[i][j]-export['bounds_unreal_cm'][i][j]) for i in range(2) for j in range(3))<.02
mat=lib.load_asset(DEST+'/M_DenseBrushFoam') or lib.duplicate_asset('/Game/UI/MenuShowcase/AnatomyV2/Materials/M_MenuContactFoam',DEST+'/M_DenseBrushFoam');assert mat
base=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_BASE_COLOR);base.set_editor_property('constant',u.LinearColor(.93,.96,1,1));vertex=edit.create_material_expression(mat,u.MaterialExpressionVertexColor);multiply=edit.create_material_expression(mat,u.MaterialExpressionMultiply);assert edit.connect_material_expressions(base,'',multiply,'A');assert edit.connect_material_expressions(vertex,'',multiply,'B');assert edit.connect_material_property(multiply,'',u.MaterialProperty.MP_BASE_COLOR)
rough=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_ROUGHNESS);rough.set_editor_property('r',.3);spec=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_SPECULAR);spec.set_editor_property('r',.38)
assert not list(edit.recompile_material(mat));mesh.set_material(0,mat);assert lib.save_loaded_asset(mat);assert lib.save_loaded_asset(mesh)
with u.ScopedEditorTransaction('Dense foam on the menu brush'):
    foam=es.spawn_actor_from_class(u.StaticMeshActor,u.Vector());foam.set_actor_label('MENU | Dense brush foam');foam.set_folder_path('Menu/Presentation');foam.tags=['MC_MenuDenseBrushFoam']
    c=foam.static_mesh_component;c.set_mobility(u.ComponentMobility.MOVABLE);c.set_static_mesh(mesh);c.set_material(0,mat);c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);c.set_editor_property('generate_overlap_events',False);c.set_cast_shadow(True);foam.set_actor_enable_collision(False);foam.set_actor_tick_enabled(False)
    assert foam.attach_to_component(hero.skeletal_mesh_component,'MenuBrush_brush',u.AttachmentRule.SNAP_TO_TARGET,u.AttachmentRule.SNAP_TO_TARGET,u.AttachmentRule.SNAP_TO_TARGET,False)
    relative_offset=[0,0,0];foam.set_actor_relative_location(u.Vector(*relative_offset),False,True);foam.set_actor_relative_rotation(u.Rotator(),False,True);foam.set_actor_relative_scale3d(u.Vector(.01,.01,.01))
assert ls.save_current_level()
for a in aa:assert re.sub('0x[0-9A-Fa-f]+','PTR',str(a.get_actor_transform()))==re.sub('0x[0-9A-Fa-f]+','PTR',r['actors_before'][a.get_path_name()])
r.update(actor=foam.get_path_name(),mesh=mesh.get_path_name(),material=mat.get_path_name(),attachment_scale=.01,relative_offset=relative_offset,world_scale=1.,triangles=export['triangles'],bubbles=export['bubble_caps'],preload='Saved actor references the new mesh and material; no lazy dynamic variants or textures.',lighting_preserved=True,postprocess_preserved=True)
(OUT/'Integration.json').write_text(json.dumps(r,indent=2),encoding='utf8');es.set_selected_level_actors([foam]);print('DENSE_MENU_FOAM_SAVED',json.dumps({k:r[k] for k in ['actor','mesh','material','triangles','bubbles','lighting_preserved','postprocess_preserved']}))





