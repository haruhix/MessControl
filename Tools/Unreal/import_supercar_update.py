"""Import/verify nine current SuperCar meshes; retain prior paired wheel assets.

Source pixels are pending from the 3D artist. Existing Buster materials are the
approved interim presentation. No original Buster mesh/material/map is modified.
"""
from pathlib import Path
import datetime, hashlib, json, os, uuid

PROJECT=Path('E:/DEVGAME/MessControl')
SOURCE=Path('E:/CodexWork/MessControl/ActiveSkills_20261029/SuperCarUpdate01')
PROPS='/Game/Gameplay/ActiveSkills/Props'
MATERIALS='/Game/Gameplay/ActiveSkills/Materials'
OWNER='MessControl.SuperCarUpdate01'

def require(value,message):
    if not value: raise RuntimeError(message)

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    import unreal as u
    require(Path(u.Paths.get_project_file_path()).resolve()==(PROJECT/'MessControl.uproject').resolve(),'Wrong project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'Stop PIE first')
    plan=json.loads((SOURCE/'IndividualWheels/assembly_handoff_9.json').read_text())
    require(plan['total_meshes']==9 and plan['total_triangles']==6982,'Unexpected source contract')
    require(json.loads((SOURCE/'IndividualWheels/fresh_import_validation_9.json').read_text())['passed'],'Nine-part fresh import validation missing')
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d_%H%M%SZ')+'_'+uuid.uuid4().hex[:8]
    out=SOURCE/('UEImport_'+stamp);out.mkdir()
    temp=out/'Temp';temp.mkdir()
    prior_env={k:os.environ.get(k) for k in ('TEMP','TMP')};os.environ['TEMP']=os.environ['TMP']=str(temp)
    lib=u.EditorAssetLibrary;tools=u.AssetToolsHelpers.get_asset_tools();sub=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    dirty=lambda:sorted(p.get_name() for p in list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))
    before_dirty=dirty();world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world();map_before=world.get_path_name() if world else None
    cvar='Interchange.FeatureFlags.Import.FBX';prior_fbx=u.SystemLibrary.get_console_variable_int_value(cvar)
    report={'passed':False,'pid':os.getpid(),'map_before':map_before,'dirty_before':before_dirty,'imports':[],'saved':[],'backup_policy':'Four individual wheel destinations are new. Five matching prior assets are verified/reused; paired wheels, Buster assets and all materials remain unchanged.','authored_PBR':'Pending separate textures from 3D artist; existing Buster materials approved interim.'}
    owned_paths={PROPS+'/'+d['name'] for d in plan['meshes']}
    require(not set(before_dirty)&owned_paths,'Unsaved SuperCar package edits must be preserved')
    def fingerprint(desc):
        uv=[];top=[]
        for i in range(desc.get_vertex_instance_count()):
            vi=u.VertexInstanceID(i);require(desc.is_vertex_instance_valid(vi),'Sparse corner IDs need inspection')
            uv.append([list(desc.get_vertex_instance_uv(vi,c).to_tuple()) for c in range(2)])
        for i in range(desc.get_triangle_count()):
            t=u.TriangleID(i);require(desc.is_triangle_valid(t),'Sparse triangle IDs need inspection')
            top.append([desc.get_vertex_instance_vertex(desc.get_triangle_vertex_instance(t,c)).get_editor_property('id_value') for c in range(3)])
        sha=lambda x:hashlib.sha256(json.dumps(x,separators=(',',':')).encode()).hexdigest()
        return {'uv_sha256':sha(uv),'topology_sha256':sha(top),'vertices':desc.get_vertex_count(),'triangles':desc.get_triangle_count()}
    def expected_local(d):
        basis=(1.,-1.,1.)
        a=[(d['bounds_m']['min'][k]-d['pivot_m'][k])*100*basis[k] for k in range(3)]
        b=[(d['bounds_m']['max'][k]-d['pivot_m'][k])*100*basis[k] for k in range(3)]
        return [(min(x,y)+max(x,y))*.5 for x,y in zip(a,b)],d['bounds_m']['dimensions']
    try:
        u.SystemLibrary.execute_console_command(None,cvar+' 0')
        for d in plan['meshes']:
            name=d['name'];dest=PROPS+'/'+name;fbx=Path(d['fbx']);sha=digest(fbx)
            mat_name='MI_Buster_Driver_Rubber' if 'Wheel' in name else ('MI_Buster_Driver_Rollers' if 'Brush' in name else 'MI_Buster_Driver_Chassis')
            material=lib.load_asset(MATERIALS+'/'+mat_name);require(isinstance(material,u.MaterialInterface),'Approved Buster material missing: '+mat_name)
            mesh=lib.load_asset(dest);reused=mesh is not None
            row={'name':name,'source':str(fbx),'sha256':sha,'destination':dest,'existing_before':reused,'backup':None,'material':material.get_path_name()}
            report['imports'].append(row)
            if reused:
                require(isinstance(mesh,u.StaticMesh) and lib.get_metadata_tag(mesh,'MCSuperCar.Owner')==OWNER and lib.get_metadata_tag(mesh,'MCSuperCar.SourceSHA256')==sha,'Preserve existing artist/different SuperCar asset: '+dest)
            else:
                options=u.FbxImportUI();options.set_editor_properties(dict(automated_import_should_detect_type=False,mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH,original_import_type=u.FBXImportType.FBXIT_STATIC_MESH,import_mesh=True,import_as_skeletal=False,import_materials=False,import_textures=False,import_animations=False,override_full_name=True))
                options.static_mesh_import_data.set_editor_properties(dict(combine_meshes=False,auto_generate_collision=False,generate_lightmap_u_vs=False,normal_import_method=u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,build_nanite=False,convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,transform_vertex_to_absolute=False,bake_pivot_in_vertex=False,import_uniform_scale=1.,import_translation=u.Vector(),import_rotation=u.Rotator(),remove_degenerates=False))
                task=u.AssetImportTask();task.filename=str(fbx);task.destination_path=PROPS;task.destination_name=name;task.automated=True;task.replace_existing=False;task.save=False;task.factory=u.FbxFactory();task.options=options
                tools.import_asset_tasks([task]);paths=list(task.imported_object_paths)
                row['imported_object_paths']=[str(p) for p in paths]
                require(len(paths)==1 and str(paths[0]).split('.')[0]==dest,'Unexpected single-part FBX destination: '+str(paths))
                mesh=lib.load_asset(paths[0]);require(isinstance(mesh,u.StaticMesh),'Import did not create StaticMesh')
                raw=list((mesh.get_bounds().box_extent*2).to_tuple());wanted=[x*100 for x in d['bounds_m']['dimensions']]
                row['raw_dimensions_cm']=raw
                # Proven UE local-vertex branch leaves metre-valued positions.
                # Measure first; accept only exact identity or x100 unit cases.
                close=lambda factor:all(abs(a*factor-b)<max(.25,abs(b)*.003) for a,b in zip(raw,wanted))
                factor=1. if close(1.) else (100. if close(100.) else None)
                require(factor is not None,'Unexpected unit/axis scale; leave unsaved for inspection: '+name+str(raw))
                desc=mesh.get_static_mesh_description(0);require(desc is not None,'Missing MeshDescription')
                before=fingerprint(desc);row['before_units_fingerprint']=before
                if factor!=1.:
                    clone=u.SystemLibrary.duplicate_object(desc,u.find_object(None,'/Engine/Transient'),'SuperCarUnits_'+uuid.uuid4().hex)
                    for i in range(clone.get_vertex_count()):
                        vi=u.VertexID(i);require(clone.is_vertex_valid(vi),'Sparse vertex IDs need inspection');clone.set_vertex_position(vi,clone.get_vertex_position(vi)*factor)
                    require(fingerprint(clone)==before,'Unit conversion changed UV/topology')
                    mesh.build_from_static_mesh_descriptions([clone],build_simple_collision=False,fast_build=False)
                require(fingerprint(mesh.get_static_mesh_description(0))==before,'Build changed source UV/topology')
                row['local_position_multiplier']=factor
                lib.set_metadata_tag(mesh,'MCSuperCar.Owner',OWNER);lib.set_metadata_tag(mesh,'MCSuperCar.SourceSHA256',sha);lib.set_metadata_tag(mesh,'MCSuperCar.AuthoredPBR','Pending3DArtistTextures;ApprovedInterimBusterMaterial')
                for slot in range(len(mesh.get_editor_property('static_materials'))):mesh.set_material(slot,material)
            size=list((mesh.get_bounds().box_extent*2).to_tuple());center=list(mesh.get_bounds().origin.to_tuple());wanted_center,dims=expected_local(d)
            require(all(abs(a-b*100)<max(.25,abs(b*100)*.003) for a,b in zip(size,dims)),'Final dimensions differ: '+name)
            require(all(abs(a-b)<.3 for a,b in zip(center,wanted_center)),'Local pivot/handedness differs: '+name)
            require(mesh.get_num_triangles(0)==d['triangles'] and sub.get_num_uv_channels(mesh,0)>=2,'Source triangle/UV count changed: '+name)
            require(list(sub.get_lod_build_settings(mesh,0).get_editor_property('build_scale3d').to_tuple())==[1.,1.,1.],'Final build scale must be identity')
            require(all(mesh.get_material(i)==material for i in range(len(mesh.get_editor_property('static_materials')))),'Material assignment differs')
            row.update(final_dimensions_cm=size,local_center_cm=center,assembly_translation_cm=d['pivot_ue_cm'],triangles=mesh.get_num_triangles(0),uv_channels=sub.get_num_uv_channels(mesh,0))
        # Save only after all nine actual imports pass the geometry/material checks.
        (out/'BeforeSave.json').write_text(json.dumps(report,indent=2))
        require(not (set(dirty())-set(before_dirty)-owned_paths),'Unexpected unrelated package became dirty; no saves performed')
        for d,row in zip(plan['meshes'],report['imports']):
            if row['existing_before']:continue
            path=PROPS+'/'+d['name'];mesh=lib.load_asset(path)
            require(lib.save_loaded_asset(mesh,only_if_is_dirty=False),'Owned mesh save failed: '+path);report['saved'].append(path)
        require(dirty()==before_dirty,'Dirty package set changed; preserve for inspection')
        after=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world();require((after.get_path_name() if after else None)==map_before,'Map changed unexpectedly')
        report.update(passed=True,dirty_after=dirty(),map_after=map_before,validation='9 actual UE mesh unit/pivot/bounds/triangle/UV/material checks; four individual wheels added, previous pairs retained; no PIE/build run by this script.')
    except Exception as error:
        report.update(error=str(error),dirty_after=dirty());raise
    finally:
        u.SystemLibrary.execute_console_command(None,cvar+' '+str(prior_fbx))
        for k,value in prior_env.items():
            if value is None:os.environ.pop(k,None)
            else:os.environ[k]=value
        (out/'ImportReport.json').write_text(json.dumps(report,indent=2))
    print('SUPERCAR_IMPORT_PASS '+str(out/'ImportReport.json'))

if __name__=='__main__':main()
