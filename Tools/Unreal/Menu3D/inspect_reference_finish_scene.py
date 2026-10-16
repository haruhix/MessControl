"""Read-only live native scene and rendering contract for reference finish."""
import json
from pathlib import Path
import unreal as u

world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world
report={'world':world.get_path_name(),'dirty_maps':[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
    'actors':[],'lights':[],'cameras':[]}
for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
    if not actor.get_actor_label().startswith('Menu3D_'):continue
    item={'name':actor.get_actor_label(),'class':actor.get_class().get_name(),'tags':[str(x) for x in actor.tags],
        'location':str(actor.get_actor_location()),'rotation':str(actor.get_actor_rotation()),'scale':str(actor.get_actor_scale3d())}
    mesh=actor.get_component_by_class(u.SkeletalMeshComponent) or actor.get_component_by_class(u.StaticMeshComponent)
    if mesh:
        item['materials']=[mesh.get_material(i).get_path_name() if mesh.get_material(i) else None for i in range(mesh.get_num_materials())]
        item['cast_shadow']=mesh.get_editor_property('cast_shadow')
    light=actor.get_component_by_class(u.LightComponent)
    if light:
        item['intensity']=light.get_editor_property('intensity');item['color']=str(light.get_editor_property('light_color'))
        item['cast_shadows']=light.get_editor_property('cast_shadows')
        if isinstance(light,u.RectLightComponent):
            for key in ('intensity_units','source_width','source_height','attenuation_radius'):
                item[key]=str(light.get_editor_property(key))
        report['lights'].append(item)
    camera=actor.get_component_by_class(u.CameraComponent)
    if camera:
        item['fov']=camera.field_of_view;item['aspect']=camera.aspect_ratio
        pp=camera.get_editor_property('post_process_settings')
        item['post_process']={key:str(pp.get_editor_property(key)) for key in ('auto_exposure_method','auto_exposure_bias','tone_curve_amount','ambient_occlusion_intensity','bloom_intensity','motion_blur_amount')}
        report['cameras'].append(item)
    report['actors'].append(item)
out=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/Evidence/v08_scene_before.json'
out.write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('MC_REF_FINISH_SCENE '+json.dumps({'world':report['world'],'dirty_maps':report['dirty_maps'],'actors':len(report['actors']),'lights':report['lights'],'cameras':report['cameras']}))
