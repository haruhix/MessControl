"""Use the actual gameplay Niagara foam, with a dense menu-only bubble preset."""
import unreal as u,json,runpy,shutil,hashlib,re
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuNiagaraBubbles20261023';OUT.mkdir(parents=True,exist_ok=True)
TARGET='/Game/UI/MenuShowcase/NiagaraBrushFoam20261023/NS_MenuBrushFoam_Dense'
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem);assert not ls.is_in_play_in_editor();world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world();assert world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
aa=list(es.get_all_level_actors());assert not any('MC_MenuDenseBrushFoam' in [str(t) for t in a.tags] for a in aa),'Preserve the user removal of the geometry foam'
assert not any('MC_MenuNiagaraBrushFoam' in [str(t) for t in a.tags] for a in aa)
backup=OUT/'Before';backup.mkdir(exist_ok=True)
if not (backup/'L_MainMenu_ArtistState.umap').exists():
    shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',backup/'L_MainMenu_Disk.umap');assert ls.save_current_level();shutil.copy2(ROOT/'Content/Maps/L_MainMenu.umap',backup/'L_MainMenu_ArtistState.umap')
hero=next(a for a in aa if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);camera=next(a for a in aa if 'MC_MenuV2_Camera' in [str(t) for t in a.tags]);report={'actors_before':{a.get_path_name():str(a.get_actor_transform()) for a in aa},'lighting':{},'postprocess':{},'camera':camera.get_path_name(),'fov':camera.camera_component.field_of_view,'source':'/Game/Gameplay/VFX/NS_BrushFoam','target':TARGET,'removed_mesh_foam_remains_absent':True,'bone':'MenuBrush_brush'}
for a in aa:
    if isinstance(a,u.Light):report['lighting'][a.get_path_name()]=u.ToolsetLibrary.get_object_properties(a.light_component,['Intensity','LightColor','Mobility'])
    if isinstance(a,u.PostProcessVolume):report['postprocess'][a.get_path_name()]=str(a.settings)
module=runpy.run_path(str(ROOT/'Tools/Unreal/author_menu_brush_foam_v2.py'));g=module['main'].__globals__;g.update(TARGET=TARGET,REPORT=OUT/'SystemAuthoring.json',PROFILES={
    'foam':{'rate':480.,'lifetime':[.32,.60],'size_cm':[6.,10.],'plane_cm':[42.,38.],'normal_offset_cm':1.5,'velocity_cm_s':[1.,5.],'cone_deg':70.,'drag':.8},
    'bubbles':{'rate':1000.,'lifetime':[1.3,2.0],'size_cm':[2.6,9.],'plane_cm':[38.,34.],'normal_offset_cm':3.,'velocity_cm_s':[15.,30.],'cone_deg':35.,'drag':.1}})
if u.EditorAssetLibrary.does_asset_exist(TARGET):g['MC_MENU_FOAM_RESUME_CREATED_TARGET']=True
asset_report=module['main']();asset_report['steady_state_estimate']={'contact_foam_mean_live':480*(.32+.60)/2,'floating_bubbles_mean_live':1000*(1.3+2.)/2,'note':'Rate times mean lifetime estimate, not a GPU count or profile.'};(OUT/'SystemAuthoring.json').write_text(json.dumps(asset_report,indent=2))
system=u.load_asset(TARGET);assert system
# Use the existing brush-local frame fitted from the artist bristles. +Z points
# away from enamel as in the native gameplay contact effect.
fit=json.loads((ROOT/'ArtSource/UI/MenuDenseBrushFoam20261023/ExportReport.json').read_text());n=u.Vector(*fit['normal_in_bone']);center=u.Vector(*fit['center_in_bone_cm'])*.01
relative=center+n*.05;rotation=u.MathLibrary.make_rot_from_z(-n)
with u.ScopedEditorTransaction('Gameplay Niagara foam with many menu bubbles'):
    actor=es.spawn_actor_from_class(u.NiagaraActor,u.Vector());actor.set_actor_label('MENU | Niagara brush foam');actor.set_folder_path('Menu/Presentation');actor.tags=['MC_MenuNiagaraBrushFoam'];actor.set_actor_enable_collision(False)
    c=actor.get_component_by_class(u.NiagaraComponent);c.set_auto_activate(False);c.set_asset(system);c.set_cast_shadow(False);c.set_editor_property('allow_scalability',False)
    assert actor.attach_to_component(hero.skeletal_mesh_component,report['bone'],u.AttachmentRule.SNAP_TO_TARGET,u.AttachmentRule.SNAP_TO_TARGET,u.AttachmentRule.SNAP_TO_TARGET,False)
    actor.set_actor_relative_location(relative,False,True);actor.set_actor_relative_rotation(rotation,False,True);actor.set_actor_relative_scale3d(u.Vector(.01,.01,.01))
    # Follow brush position, but keep the effect orientation fixed in world space.
    world_rotation=actor.get_actor_rotation();c.set_absolute(False,True,False);actor.set_actor_rotation(world_rotation,False)
    c.set_tick_behavior(u.NiagaraTickBehavior.FORCE_TICK_LAST)
    hero.skeletal_mesh_component.set_editor_property('visibility_based_anim_tick_option',u.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    c.set_auto_activate(True);c.activate(True)
assert ls.save_current_level()
for a in aa:assert re.sub('0x[0-9A-Fa-f]+','PTR',str(a.get_actor_transform()))==re.sub('0x[0-9A-Fa-f]+','PTR',report['actors_before'][a.get_path_name()])
report.update(actor=actor.get_path_name(),relative_offset=list(relative.to_tuple()),relative_rotation=str(rotation),attachment_scale=.01,foam_rate=480.,bubble_rate=1000.,estimated_live=asset_report['steady_state_estimate'],protected_hashes=asset_report['protected_package_hashes_after'],preload='Saved NiagaraComponent references the system; it references the original gameplay foam and bubble materials. No new materials, textures or dynamic variants.',lighting_preserved=True,postprocess_preserved=True)
(OUT/'Integration.json').write_text(json.dumps(report,indent=2),encoding='utf8');es.set_selected_level_actors([actor]);print('GAMEPLAY_NIAGARA_MENU_FOAM_SAVED',json.dumps({k:report[k] for k in ['actor','source','target','foam_rate','bubble_rate','estimated_live']}))
