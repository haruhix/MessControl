"""Bake contact from the existing fixed artist pose; never remodel the source.

Run in an owned full Editor with -MCThroatContactAuthorOwned and GeometryScripting
enabled. The active map must be L_Mouth. Saves only the new contact mesh and the
two inspected nut profiles. No map, source skeletal mesh or material is saved.
"""
import hashlib
import json
import pathlib
import time
import traceback
import unreal as u

ROOT=pathlib.Path(u.Paths.project_dir()).resolve()
OUT=pathlib.Path('E:/CodexWork/MessControlFeedback_20261019B/ThroatContact')
DEST='/Game/Gameplay/Arena/ManualIntake/SM_ThroatTissueContact'
REV='throat_contact_v1_exp075'
if '-MCThroatContactAuthorOwned' not in u.SystemLibrary.get_command_line():
    raise RuntimeError('Use the owned authoring launcher')
if OUT.joinpath('Result.json').exists():raise RuntimeError('Preserve existing authoring evidence')
OUT.mkdir(parents=True,exist_ok=True)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert 'L_Mouth' in world.get_path_name() and not levels.is_in_play_in_editor()
assert not list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
assert not list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())
throats=[a for a in actors.get_all_level_actors() if isinstance(a,u.MCThroat)]
assert len(throats)==1
throat=throats[0]
mouth=throat.get_editor_property('authored_mouth')
source=mouth.get_skeletal_mesh_asset()
assert source.get_path_name()=='/Game/FromBlender4/SK_Exit.SK_Exit'
assert abs(float(throat.get_editor_property('manual_authored_expansion'))-.75)<1e-5
source_file=ROOT/'Content/FromBlender4/SK_Exit.uasset'
source_hash=hashlib.sha256(source_file.read_bytes()).hexdigest()
report={'status':'preparing','source':source.get_path_name(),'sourceSha256':source_hash,
        'expansion':.75,'openMorph':.25,'mapSaved':False,'profiles':[],'probes':[]}
def save():
    (OUT/'Result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
save()
camera=actors.spawn_actor_from_class(u.CameraActor,u.Vector(1100,-110,1000))
target=u.MathLibrary.transform_location(mouth.get_world_transform(),u.Vector(0,300,-100))
camera.set_actor_rotation(u.MathLibrary.find_look_at_rotation(camera.get_actor_location(),target),False)
camera.camera_component.set_field_of_view(70)
u.EditorLevelLibrary.pilot_level_actor(camera)
levels.editor_set_game_view(True)
mouth.set_forced_lod(1)
mouth.set_update_animation_in_editor(True)
mouth.set_morph_target('Open',.25)
mouth.set_morph_target('vomit',0)
held=[camera,throat,mouth,source]

class Author:
    def __init__(self):
        self.started=time.monotonic();self.frames=0;self.phase=0;self.shot=None;self.preview=None
        self.busy=False;self.finished=False
    def finish(self,error=None):
        if self.finished:return
        self.finished=True
        u.unregister_slate_post_tick_callback(self.callback)
        u.EditorLevelLibrary.eject_pilot_level_actor()
        if self.preview:actors.destroy_actor(self.preview)
        actors.destroy_actor(camera)
        report.update(status='failed' if error else 'authored_pending_native_validation',error=error,
                      sourceUnchanged=hashlib.sha256(source_file.read_bytes()).hexdigest()==source_hash)
        save()
        u.log('MC_THROAT_CONTACT '+report['status'])
        u.EditorPythonScripting.set_keep_python_script_alive(False)
    def tick(self,dt):
        # Asset compilation/build can pump Slate recursively. Never re-enter an
        # authoring phase while its previous callback is still saving a mesh.
        if self.busy or self.finished:return
        self.busy=True
        try:
            if time.monotonic()-self.started>180:raise RuntimeError('Bounded authoring timeout')
            self.frames+=1
            if self.phase==0:
                if self.frames<100 or time.monotonic()-self.started<6:return
                report['copyApi']=u.GeometryScript_SceneUtils.copy_mesh_from_component.__doc__
                dyn=u.DynamicMesh()
                opt=u.GeometryScriptCopyMeshFromComponentOptions()
                lod=u.GeometryScriptMeshReadLOD()
                lod.set_editor_properties(dict(lod_type=u.GeometryScriptLODType.RENDER_DATA,lod_index=0))
                opt.set_editor_property('requested_lod',lod)
                copied=u.GeometryScript_SceneUtils.copy_mesh_from_component(mouth,dyn,opt,False)
                assert copied[-1]==u.GeometryScriptOutcomePins.SUCCESS,str(copied)
                assert dyn.get_triangle_count()>100
                report['triangles']=dyn.get_triangle_count()
                held.append(dyn)
                if u.EditorAssetLibrary.does_asset_exist(DEST):
                    mesh=u.EditorAssetLibrary.load_asset(DEST)
                    assert u.EditorAssetLibrary.get_metadata_tag(mesh,'MCContactRevision')==REV,'Unowned contact destination'
                    assert u.EditorAssetLibrary.get_metadata_tag(mesh,'MCContactSourceSha256')==source_hash,'Source changed; review a new bake'
                    updated=u.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(dyn,mesh,u.GeometryScriptCopyMeshToAssetOptions(),u.GeometryScriptMeshWriteLOD())
                    assert updated[-1]==u.GeometryScriptOutcomePins.SUCCESS,str(updated)
                else:
                    opts=u.GeometryScriptCreateNewStaticMeshAssetOptions()
                    opts.set_editor_properties(dict(enable_collision=True,collision_mode=u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE,enable_nanite=False))
                    created=u.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dyn,DEST,opts)
                    mesh=created[0] if isinstance(created,tuple) else created
                assert isinstance(mesh,u.StaticMesh),str(created)
                body=mesh.get_editor_property('body_setup')
                body.set_editor_property('double_sided_geometry',True)
                edit=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
                for section in range(mesh.get_num_sections(0)):edit.enable_section_collision(mesh,True,0,section)
                u.EditorAssetLibrary.set_metadata_tag(mesh,'MCContactRevision',REV)
                u.EditorAssetLibrary.set_metadata_tag(mesh,'MCContactSourceSha256',source_hash)
                u.EditorAssetLibrary.set_metadata_tag(mesh,'MCContactExpansion','.75')
                assert u.EditorAssetLibrary.save_loaded_asset(mesh,False)
                held.append(mesh)
                self.preview=actors.spawn_actor_from_class(u.StaticMeshActor,u.Vector(0,0,0))
                self.preview.set_actor_label('TEMP | Exact throat contact probes')
                comp=self.preview.static_mesh_component
                comp.set_static_mesh(mesh)
                self.preview.set_actor_transform(mouth.get_world_transform(),False,True)
                comp.set_collision_profile_name('BlockAll')
                comp.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS)
                comp.set_visibility(False)
                self.comp=comp
                self.phase=1;self.frames=0;return
            if self.phase==1:
                if self.frames<20:return
                transform=mouth.get_world_transform()
                normal=u.Vector(0,.9747185974,.2234360218)
                aim=throat.get_intake_aim_target()
                center=u.MathLibrary.inverse_transform_location(transform,aim[0])
                report['visibleAimLocal']=str(center)
                report['actualOpenMorph']=mouth.get_morph_target('Open')
                points=[('open_hole',center,False),('left_tissue',center+u.Vector(-650,0,0),True),('right_tissue',center+u.Vector(650,0,0),True)]
                errors=[]
                for name,point,expected in points:
                    start=u.MathLibrary.transform_location(transform,point-normal*1200)
                    end=u.MathLibrary.transform_location(transform,point+normal*1200)
                    hit=self.comp.line_trace_component(start,end,True,False,False)
                    # The Python wrapper uses the native bool as success:
                    # false -> None, true -> the four output parameters.
                    result=hit is not None
                    report['probes'].append(dict(name=name,expected=expected,hit=result,raw=str(hit)))
                    if result!=expected:errors.append('Contact probe failed: '+name+' '+str(hit))
                self.probeError='\n'.join(errors) if errors else None
                for path in ['/Game/Gameplay/CoreLoop/DA_NutRain','/Game/Gameplay/CoreLoop/NutWizard/MCPerformanceV7/DA_NutRain_PerformanceV7']:
                    profile=u.EditorAssetLibrary.load_asset(path)
                    settings=profile.get_editor_property('settings');boss=settings.get_editor_property('boss')
                    before=float(boss.get_editor_property('roll_speed'))
                    assert abs(before-520)<.01 or abs(before-676)<.01,path+' unexpected roll speed'
                    if abs(before-520)<.01:
                        disk=ROOT/'Content'/ (path.removeprefix('/Game/')+'.uasset')
                        backup=OUT/'Backups'/disk.relative_to(ROOT/'Content')
                        backup.parent.mkdir(parents=True,exist_ok=True);backup.write_bytes(disk.read_bytes())
                        boss.set_editor_property('roll_speed',676)
                        settings.set_editor_property('boss',boss);profile.set_editor_property('settings',settings)
                        assert u.EditorAssetLibrary.save_loaded_asset(profile,False)
                    report['profiles'].append(dict(path=path,before=before,after=float(profile.get_editor_property('settings').get_editor_property('boss').get_editor_property('roll_speed'))))
                self.shot=u.AutomationLibrary.take_high_res_screenshot(1280,720,str(OUT/'UnchangedArtistThroat.png'),camera=camera,force_game_view=True)
                self.phase=2;return
            if self.phase==2 and self.shot.is_task_done() and (OUT/'UnchangedArtistThroat.png').exists():
                self.finish(self.probeError)
        except Exception:self.finish(traceback.format_exc())
        finally:self.busy=False

author=Author()
author.callback=u.register_slate_post_tick_callback(author.tick)
u.EditorPythonScripting.set_keep_python_script_alive(True)
