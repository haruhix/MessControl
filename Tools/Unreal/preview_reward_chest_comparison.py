"""Native/BP chest offscreen comparison in a discarded temporary editor map.

Owning editor only; no PIE/build/package saves. Refuses any unsaved work. It
restores the original saved map, viewport, selection, and realtime thumbnail
state after capture (also on failure). Keep this callback alive until Report.json
says complete; cancel only via builtins.mc_reward_chest_preview.cancel().
"""
import builtins
import json
import time
import traceback
from pathlib import Path
import unreal as u

KEY = 'mc_reward_chest_preview'
OUT = Path(globals().get('MC_CHEST_PREVIEW_OUTPUT',
    'E:/CodexWork/HubChestWardrobe_20261022/ChestComparison')).resolve()
assert OUT.drive.upper() == 'E:'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved content edits'
assert not u.EditorLevelLibrary.get_pie_worlds(False), 'Stop only an owned Play first'
assert not hasattr(builtins, KEY), 'Chest comparison is already active'
OUT.mkdir(parents=True, exist_ok=True)

def vec(v): return [v.x, v.y, v.z]
def path(o): return o.get_path_name() if o else None

class ChestComparison:
    def __init__(self):
        self.actors = u.get_editor_subsystem(u.EditorActorSubsystem)
        self.editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
        self.levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
        self.original_map = self.editor.get_editor_world().get_path_name().split('.')[0]
        assert self.original_map.startswith('/Game/'), 'Original map must be a saved project map'
        self.original_file = Path(u.Paths.project_content_dir()) / (self.original_map.removeprefix('/Game/')+'.umap')
        assert self.original_file.exists(), 'Original saved map file must exist'
        self.view = self.editor.get_level_viewport_camera_info()
        self.selection = [path(a) for a in self.actors.get_selected_level_actors()]
        self.spawned, self.chests = [], []
        self.handle = None
        self.started = time.monotonic()
        self.case = self.warm = 0
        self.report = dict(status='running', original_map=self.original_map,
            capture='Actual Unreal offscreen editor render, isolated discarded map; not gameplay',
            outputs=[], models=[], original_selection=self.selection)

    def write(self):
        (OUT / 'Report.json').write_text(json.dumps(self.report, indent=2), encoding='utf-8')

    def spawn(self, cls, location=u.Vector(), rotation=u.Rotator()):
        actor = self.actors.spawn_actor_from_class(cls, location, rotation, transient=True)
        assert actor, 'Failed to create preview actor: ' + str(cls)
        actor.tags = [u.Name('MC_TemporaryChestPreview')]
        self.spawned.append(actor)
        return actor

    def begin(self):
        try:
            self.world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
            assert self.world and self.world.get_path_name().split('.')[0] != self.original_map
            self.report['temporary_map'] = self.world.get_path_name()
            for cls, name in [(u.MCRewardChest, 'Native'),
                    (u.load_asset('/Game/Gameplay/Roguelike/BP_RewardChest').generated_class(), 'Blueprint')]:
                chest = self.spawn(cls)
                chest.set_actor_hidden_in_game(False)
                chest.set_is_temporarily_hidden_in_editor(False)
                self.chests.append(chest)
                bottom_override=globals().get('MC_CHEST_PREVIEW_BOTTOM_OVERRIDE')
                top_override=globals().get('MC_CHEST_PREVIEW_TOP_OVERRIDE')
                assert bool(bottom_override)==bool(top_override), 'Both mesh overrides are required'
                if bottom_override:
                    bottom=u.load_asset(bottom_override);top=u.load_asset(top_override)
                    assert isinstance(bottom,u.StaticMesh) and isinstance(top,u.StaticMesh)
                    body=chest.get_editor_property('body');lid=chest.get_editor_property('lid')
                    pivot=chest.get_editor_property('lid_pivot')
                    body.set_static_mesh(bottom);lid.set_static_mesh(top)
                    # Same bottom-grounded seating math as ConfigureGeometry;
                    # explicit because editor-only preview does not call BeginPlay.
                    scale=float(chest.get_editor_property('model_scale'))
                    bb=bottom.get_bounding_box();tb=top.get_bounding_box()
                    hinge_x=tb.min.x*scale
                    hinge_z=(bb.max.z-bb.min.z+float(chest.get_editor_property('lid_seat_offset')))*scale
                    body.set_relative_scale3d(u.Vector(scale,scale,scale))
                    body.set_relative_location(u.Vector(0,0,-bb.min.z*scale),False,False)
                    pivot.set_relative_location(u.Vector(hinge_x,0,hinge_z),False,False)
                    lid.set_relative_scale3d(u.Vector(scale,scale,scale))
                    lid.set_relative_location(u.Vector(-hinge_x,0,-tb.min.z*scale),False,False)
                override=globals().get('MC_CHEST_PREVIEW_MATERIAL_OVERRIDE')
                if override:
                    candidate=u.load_asset(override)
                    assert candidate, 'Preview material override must already exist'
                    chest.get_editor_property('body').set_material(0,candidate)
                    chest.get_editor_property('lid').set_material(0,candidate)
                self.report['models'].append(dict(case=name,
                    body=path(chest.get_editor_property('body').static_mesh),
                    body_material=path(chest.get_editor_property('body').get_material(0)),
                    lid=path(chest.get_editor_property('lid').static_mesh),
                    lid_material=path(chest.get_editor_property('lid').get_material(0)),
                    lid_hinge=vec(chest.get_editor_property('lid_pivot').relative_location),
                    body_location=vec(chest.get_editor_property('body').relative_location)))
                # Landing indicator/cavity are not part of the closed reward model.
                for prop in ['telegraph', 'mimic_mouth', 'mimic_lower_teeth', 'mimic_upper_teeth']:
                    chest.get_editor_property(prop).set_visibility(False)
                chest.get_editor_property('rogue_label').set_visibility(False)
            floor = self.spawn(u.StaticMeshActor, u.Vector(0,0,-3))
            floor.static_mesh_component.set_static_mesh(u.load_asset('/Engine/BasicShapes/Plane'))
            floor.set_actor_scale3d(u.Vector(30,30,1))
            floor_mat = floor.static_mesh_component.create_dynamic_material_instance(0,
                u.load_asset('/Engine/BasicShapes/BasicShapeMaterial'))
            floor_mat.set_vector_parameter_value('Color', u.LinearColor(.035,.045,.055,1))
            for cls, position, color, intensity in [
                    (u.DirectionalLight, (200,-300,400), (1.,.83,.64), 3.5),
                    (u.DirectionalLight, (-200,120,300), (.52,.76,1.), 1.5),
                    (u.DirectionalLight, (-220,-30,400), (.72,1.,.88), 2.)]:
                location=u.Vector(*position)
                light = self.spawn(cls, location, u.MathLibrary.find_look_at_rotation(location,u.Vector(0,0,55)))
                comp = light.get_component_by_class(u.DirectionalLightComponent)
                comp.set_intensity(intensity)
                comp.set_light_color(u.LinearColor(*color,1.))
            self.capture = self.spawn(u.SceneCapture2D)
            self.cc = self.capture.get_editor_property('capture_component2d')
            self.target = u.RenderingLibrary.create_render_target2d(self.world,1200,1000,
                u.TextureRenderTargetFormat.RTF_RGBA8,u.LinearColor(.025,.03,.04,1),False,False)
            self.cc.set_editor_properties(dict(texture_target=self.target,
                capture_source=u.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
                capture_every_frame=False,capture_on_movement=False,
                always_persist_rendering_state=True,fov_angle=32.))
            self.cases = [('Native_closed',0,0,(285,-350,225)),
                ('Blueprint_closed',1,0,(285,-350,225)),
                ('Native_open',0,105,(310,-370,255)),
                ('Blueprint_open',1,105,(310,-370,255)),
                ('Native_back_closed',0,0,(-285,350,210)),
                ('Blueprint_back_closed',1,0,(-285,350,210))]
            self.prepare_case()
            self.write()
            self.handle = u.register_slate_post_tick_callback(self.tick)
        except Exception:
            self.finish('failed', traceback.format_exc())

    def prepare_case(self):
        name, index, angle, eye = self.cases[self.case]
        for i, chest in enumerate(self.chests):
            chest.set_is_temporarily_hidden_in_editor(i != index)
            chest.set_actor_hidden_in_game(i != index)
        chest=self.chests[index]
        chest.get_editor_property('lid_pivot').set_relative_rotation(u.Rotator(pitch=angle),False,False)
        eye=u.Vector(*eye)
        self.capture.set_actor_location_and_rotation(eye,
            u.MathLibrary.find_look_at_rotation(eye,u.Vector(0,0,60)),False,True)
        self.warm=0
        self.prepared_at=time.monotonic()

    def tick(self, dt):
        try:
            assert time.monotonic()-self.started<90., 'Preview timed out'
            self.cc.capture_scene()
            self.warm+=1
            if self.warm<12 or time.monotonic()-self.prepared_at<.5:
                return
            name=self.cases[self.case][0]
            filename=name+'.png'
            u.RenderingLibrary.export_render_target(self.world,self.target,str(OUT),filename)
            assert (OUT / filename).exists(), 'Render export missing: '+filename
            self.report['outputs'].append(str(OUT / filename))
            self.case+=1
            if self.case>=len(self.cases):
                self.finish('complete')
            else:
                self.prepare_case()
        except Exception:
            self.finish('failed', traceback.format_exc())

    def cancel(self): self.finish('cancelled')

    def finish(self, status, error=None):
        if self.handle:
            u.unregister_slate_post_tick_callback(self.handle)
            self.handle=None
        self.report.update(status=status,error=error)
        try:
            for actor in reversed(self.spawned):
                if u.SystemLibrary.is_valid(actor): self.actors.destroy_actor(actor)
            # Discard only this tool's temporary map; original was guarded clean.
            # This API explicitly discards the temporary map without a save dialog.
            assert u.EditorLoadingAndSavingUtils.load_map(str(self.original_file)), 'Failed to restore original map'
            self.editor.set_level_viewport_camera_info(*self.view)
            restored=[a for a in self.actors.get_all_level_actors() if path(a) in self.selection]
            self.actors.set_selected_level_actors(restored)
            self.report['restored_map']=self.editor.get_editor_world().get_path_name().split('.')[0]
            self.report['dirty_maps_after']=[path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
            self.report['dirty_content_after']=[path(p) for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
            assert self.report['restored_map']==self.original_map
            assert not self.report['dirty_maps_after'] and not self.report['dirty_content_after'], 'Unexpected dirty packages after restore'
        except Exception:
            self.report['restore_error']=traceback.format_exc()
            self.report['status']='failed'
        self.write()
        if getattr(builtins,KEY,None) is self:
            delattr(builtins,KEY)
        u.log('MC_CHEST_PREVIEW '+self.report['status']+' '+str(OUT / 'Report.json'))

preview=ChestComparison()
setattr(builtins,KEY,preview)
preview.begin()
