"""Same-session SM6 comparison: conventional shadows, VSM, then BC-only pigment.

Runtime only. Restores the original material, VSM cvar and widget visibility.
No renderer configuration or saved scene assignment is changed.
"""
import json
from pathlib import Path
import time
import unreal as u


class NativeVSMReview:
    def __init__(self):
        self.handle = None
        self.finished = False
        self.index = -1
        self.pending = None
        self.started = time.monotonic()
        self.world = u.EditorLevelLibrary.get_game_world()
        assert self.world and u.SystemLibrary.is_standalone(self.world)
        root = Path(u.Paths.project_dir()).resolve()
        request_path = root / 'Tools/Unreal/Menu3D/Evidence/vsm_review_request.json'
        request = json.loads(request_path.read_text(encoding='utf8')) if request_path.exists() else {}
        run_name = request.get('run_name', 'NativeVSM_v08_01')
        assert Path(run_name).name == run_name and run_name.startswith('NativeVSM_v08_')
        self.mode = request.get('mode', 'vsm')
        assert self.mode in ('vsm', 'pigment')
        self.out = root / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly/Review/Unreal_FunctionalBlocking_v03_FullRoute' / run_name
        assert not self.out.exists()
        cls = u.load_class(None, '/Game/UI/MainMenu3D/WBP_MainMenu3D.WBP_MainMenu3D_C')
        lib = u.get_default_object(u.load_class(None, '/Script/UMG.WidgetBlueprintLibrary'))
        widgets = lib.call_method('GetAllWidgetsOfClass', args=(self.world, cls, False))
        assert len(widgets) == 1
        self.widget = widgets[0]
        assert self.widget.get_editor_property('PageIndex') == 1
        assert self.widget.get_editor_property('SelectedDay') == 3 and self.widget.get_editor_property('Open3') > .98
        for name in ('r.Nanite.ProjectEnabled', 'r.Shadow.Virtual.ProjectEnabled', 'r.Shadow.Virtual.NonNanite.ProjectEnabled'):
            assert u.SystemLibrary.get_console_variable_int_value(name) == 1, name
        self.vsm_before = u.SystemLibrary.get_console_variable_int_value('r.Shadow.Virtual.Enable')
        self.visibility_before = self.widget.get_visibility()
        actor = u.GameplayStatics.get_all_actors_with_tag(self.world, 'Menu3D_DAY03')[0]
        self.component = actor.get_component_by_class(u.SkeletalMeshComponent)
        self.material_before = self.component.get_material(0)
        assert isinstance(self.material_before, u.MaterialInstanceDynamic)
        self.candidates = [u.load_asset('/Game/UI/MainMenu3D/ReferenceFinish/Materials/MI_DAY03_Enamel_WarmBody' + revision)
                           for revision in (('01', '02') if self.mode == 'pigment' else ('01',))]
        assert all(self.candidates)
        for candidate in self.candidates:
            for name in ('RoughnessTexture', 'NormalTexture', 'AOTexture', 'ThicknessTexture'):
                assert self.material_before.get_texture_parameter_value(name) == u.MaterialEditingLibrary.get_material_instance_texture_parameter_value(candidate, name)
        self.state = {'scope': 'Actual solo PIE in forced SM6 task session; same camera/geometry/light values; VSM0/VSM1 then one enamel BaseColor change',
                      'vsm_before': self.vsm_before, 'mesh': self.component.skeletal_mesh_asset.get_path_name(),
                      'mode': self.mode, 'captures': [], 'error': None, 'configuration_assets_changed': False}
        self.out.mkdir(parents=True)
        self.widget.set_visibility(u.SlateVisibility.HIT_TEST_INVISIBLE)

    def apply(self, index):
        self.component.set_material(0, self.material_before)
        u.SystemLibrary.execute_console_command(self.world, 'r.Shadow.Virtual.Enable ' + ('0' if index == 0 and self.mode == 'vsm' else '1'))
        if index == 2 or (self.mode == 'pigment' and index == 1):
            candidate = self.candidates[index-1] if self.mode == 'pigment' else self.candidates[0]
            material = self.component.create_dynamic_material_instance(0, candidate)
            for name in ('CardHalfHeight', 'OpenAmount', 'ScatterDensity', 'RoughnessScale', 'Specular'):
                material.set_scalar_parameter_value(name, self.material_before.get_scalar_parameter_value(name))
            for name in ('BaseColorTint', 'SubsurfaceTint'):
                material.set_vector_parameter_value(name, self.material_before.get_vector_parameter_value(name))
        self.index = index
        self.applied = time.monotonic()

    def start(self):
        self.apply(0)
        self.handle = u.register_slate_post_tick_callback(self.tick)

    def tick(self, delta):
        try:
            assert time.monotonic()-self.started < 90., 'Review watchdog expired'
            self.widget.call_method('ResetPointerIntent')
            if self.pending:
                path = self.out / self.pending['file']
                if path.is_file() and path.read_bytes().endswith(b'IEND\xaeB`\x82'):
                    self.pending['completed_wall'] = time.time()
                    self.pending['file_bytes'] = path.stat().st_size
                    self.state['captures'].append(self.pending)
                    self.pending = None
                    if self.index == 2:
                        self.finish()
                    else:
                        self.apply(self.index+1)
                return
            if time.monotonic()-self.applied >= 3.:
                names = ('A_original_BC_VSM', 'B_WarmBody01_VSM', 'C_WarmBody02_VSM') if self.mode == 'pigment' else ('A_SM6_Conventional', 'B_SM6_VSM', 'C_SM6_VSM_WarmBody')
                name = names[self.index]
                material = self.component.get_material(0)
                self.pending = {'file': name+'.png', 'requested_wall': time.time(),
                                'vsm': u.SystemLibrary.get_console_variable_int_value('r.Shadow.Virtual.Enable'),
                                'material': material.get_path_name(),
                                'basecolor': material.get_texture_parameter_value('BaseColorTexture').get_path_name(),
                                'open3': self.widget.get_editor_property('Open3')}
                u.SystemLibrary.execute_console_command(self.world, 'Shot SHOWUI filename='+(self.out/self.pending['file']).as_posix()+' -nosuffix')
        except Exception as error:
            self.finish(str(error))
            raise

    def finish(self, error=None):
        if self.finished:
            return
        self.finished = True
        self.component.set_material(0, self.material_before)
        u.SystemLibrary.execute_console_command(self.world, 'r.Shadow.Virtual.Enable '+str(self.vsm_before))
        self.widget.set_visibility(self.visibility_before)
        self.widget.call_method('ResetPointerIntent')
        if self.handle is not None:
            u.unregister_slate_post_tick_callback(self.handle)
        self.state['error'] = error
        self.state['vsm_restored'] = u.SystemLibrary.get_console_variable_int_value('r.Shadow.Virtual.Enable')
        self.state['material_restored'] = self.component.get_material(0).get_path_name()
        (self.out/'candidate_states.json').write_text(json.dumps(self.state, indent=2), encoding='utf8')
        u._mc_native_vsm_review = None
        u.log('MC_NATIVE_VSM_REVIEW_FINISHED '+str(error))


assert not getattr(u, '_mc_native_vsm_review', None)
review = NativeVSMReview()
u._mc_native_vsm_review = review
try:
    review.start()
except Exception as error:
    review.finish(str(error))
    raise
