"""Presentation-only point-light rig for the project's conventional shadow renderer."""
import unreal as u
from pathlib import Path
import json,shutil
OUT=Path(globals().get('MC_MENU_LIGHT_OUT','E:/CodexWork/MessControlMenuShowcase_20261021/FinalLighting01'))
assert OUT.drive.upper()=='E:' and not OUT.exists();OUT.mkdir(parents=True)
ls=u.get_editor_subsystem(u.LevelEditorSubsystem);es=u.get_editor_subsystem(u.EditorActorSubsystem)
assert not ls.is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
dirty=list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
assert all(p.get_name()=='/Game/Maps/L_MainMenu' for p in dirty)
actors=list(es.get_all_level_actors())
assert all('MC_MenuShowcase_OwnedV1' in [str(t) for t in a.tags] for a in actors)
shutil.copy2('E:/DEVGAME/MessControl/Content/Maps/L_MainMenu.umap',OUT/'L_MainMenu_Before.umap')
for a in actors:
    for c in a.get_components_by_class(u.RectLightComponent):c.set_intensity(0.)
    if a.get_actor_label() in ('MC_MenuShowcase_FinalFill','MC_MenuShowcase_FinalRim','MC_MenuShowcase_FinalBackground'):
        assert es.destroy_actor(a)
key=next(a for a in actors if a.get_actor_label()=='MC_MenuShowcase_SoftShadowKey')
key.get_component_by_class(u.PointLightComponent).set_cast_shadows(False)
specs=[('FinalFill',(480,350,180),115.,(.82,.90,1.)),('FinalRim',(290,-320,300),50.,(1.,.80,.68)),('FinalBackground',(-60,-430,340),125.,(1.,.73,.78))]
for name,position,intensity,color in specs:
    a=es.spawn_actor_from_class(u.PointLight,u.Vector(*position))
    a.set_actor_label('MC_MenuShowcase_'+name)
    a.tags=[u.Name('MC_MenuShowcase_OwnedV1'),u.Name('MC_MenuShowcase_'+name)]
    a.set_folder_path('MenuShowcase/A')
    a.set_actor_tick_enabled(False)
    c=a.get_component_by_class(u.PointLightComponent)
    c.set_mobility(u.ComponentMobility.MOVABLE)
    c.set_intensity_units(u.LightUnits.CANDELAS);c.set_intensity(intensity)
    c.set_light_color(u.LinearColor(*color,1.));c.set_attenuation_radius(2000.)
    c.set_editor_property('specular_scale',.25);c.set_editor_property('source_radius',0.)
    c.set_cast_shadows(False)
assert ls.save_current_level()
camera=next(a for a in es.get_all_level_actors() if isinstance(a,u.CameraActor))
out='E:/DEVGAME/MessControl/Artifacts/MenuShowcase_20261021/Unreal/Stage05_Viewport12_PointRig.png'
assert not Path(out).exists()
u.AutomationLibrary.take_high_res_screenshot(1600,900,out,camera=camera,delay=1.,force_game_view=True)
RESULT={'status':'pending_root_visual_review','root_diagnostic':'Oval survived source-mesh replacement, normal recompute, key shadow disable and local specular disable. It disappeared when remaining RectLights were disabled.','changes':'Point fill/rim/background; scene-only contact AO retained; global renderer/config unchanged','requested':out}
(OUT/'Report.json').write_text(json.dumps(RESULT,indent=2),encoding='utf8')
