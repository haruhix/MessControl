"""Conditional simple collision repair for the four existing Hub cabins and shop.

Plan reads current live collision, settings and paired-door orientation. Apply
requires explicit role selection after inspection; valid existing collision is
preserved unless an actual walk failure is explicitly documented. Uses private
copies of the existing meshes, never alters source artist assets, and saves only
the chosen copies and Hub. No C++ classes, new scenery, build, PIE or package cook.
"""
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil


ROOT = Path('E:/DEVGAME/MessControl')
HUB = '/Game/Maps/Hub'
FOLDER = '/Game/Gameplay/Hub/Collision'
OWNER_KEY = 'MC_HubStationCollision'
REVISION = 'BoxesV1_20261023'
ROLES = {
    'body': {'source':'/Game/FromBlender11/SM_Cube_006', 'target':FOLDER+'/SM_HubCabinBody_Collision',
             'actors':('StaticMeshActor_109','StaticMeshActor_110','StaticMeshActor_112','StaticMeshActor_113')},
    'door': {'source':'/Game/FromBlender11/SM_Cube_006_002', 'target':FOLDER+'/SM_HubCabinDoor_Collision',
             'actors':('StaticMeshActor_108','StaticMeshActor_111','StaticMeshActor_114','StaticMeshActor_115')},
    'shop': {'source':'/Game/FromBlender11/SM_BusterShop', 'target':FOLDER+'/SM_HubVending_Collision',
             'actors':('StaticMeshActor_103',)}}


def require(condition,message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def mesh_bounds(mesh):
    bounds=mesh.get_bounding_box()
    return [[float(value) for value in bounds.min.to_tuple()],
            [float(value) for value in bounds.max.to_tuple()]]


def same_mesh_bounds(first,second,tolerance=.001):
    # Static-mesh rebuild rounds imported bounds through float precision.
    # This gate allows <= ten microns, not a visible art-geometry change.
    return max(abs(a-b) for pair_a,pair_b in zip(mesh_bounds(first),mesh_bounds(second))
               for a,b in zip(pair_a,pair_b))<=tolerance


def dirty(u):
    return sorted({package.get_path_name() for package in
        list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+
        list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())})


def box(name,minimum,maximum):
    size=[b-a for a,b in zip(minimum,maximum)]
    require(all(math.isfinite(value) and value>0 for value in size),'Invalid collision box dimensions')
    return {'name':name,'center':[(a+b)*.5 for a,b in zip(minimum,maximum)],'size':size,
            'rotation':[0.,0.,0.]}


def boxes_for(role,minimum,maximum,front_axis=1,front_sign=1,wall_thickness=8.):
    if role!='body':
        return [box('MC_'+role+'_Solid',minimum,maximum)]
    require(front_axis in (0,1) and front_sign in (-1,1),'Invalid cabin front axis')
    width_axis=1-front_axis; depth_axis=front_axis
    require(wall_thickness*4<min(maximum[0]-minimum[0],maximum[1]-minimum[1]),'Cabin wall thickness closes the interior')
    result=[]
    for side in (-1,1):
        low=list(minimum);high=list(maximum)
        if side<0:
            high[width_axis]=low[width_axis]+wall_thickness
        else:
            low[width_axis]=high[width_axis]-wall_thickness
        result.append(box('MC_Cabin_Left' if side<0 else 'MC_Cabin_Right',low,high))
    low=list(minimum);high=list(maximum)
    if front_sign>0:
        high[depth_axis]=low[depth_axis]+wall_thickness
    else:
        low[depth_axis]=high[depth_axis]-wall_thickness
    result.append(box('MC_Cabin_Back',low,high))
    low=list(minimum);high=list(maximum);low[2]=high[2]-wall_thickness
    result.append(box('MC_Cabin_Top',low,high))
    return result


def mesh_file(package,suffix='.uasset'):
    require(package.startswith('/Game/') and '..' not in package,'Invalid package path')
    return (ROOT/'Content'/package[len('/Game/'):]).with_suffix(suffix)


def inspect(u,output):
    path=ROOT/'Tools/Unreal/Hub/inspect_hub_station_collision.py'
    spec=importlib.util.spec_from_file_location('mc_station_collision_inventory',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module.run(output=str(output))


def run(action='plan', *, repairs=(), wall_thickness=8.,
        confirmed_existing_failure=None,
        output='E:/CodexWork/HubWardrobeRewards_20261022/StationCollision/Author01'):
    import unreal as u
    require(action in ('plan','apply'),'Expected plan or apply')
    require(Path(u.Paths.project_dir()).resolve()==ROOT.resolve(),'Wrong checkout')
    out=Path(output).resolve();require(out.drive.upper()=='E:','All outputs must stay on E:')
    require(not u.EditorLevelLibrary.get_pie_worlds(True),'Preserve active Play')
    world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    require(world and world.get_path_name().split('.',1)[0]==HUB,'Open Hub before authoring')
    require(not dirty(u),'Preserve unsaved editor packages before authoring')
    require(math.isfinite(wall_thickness) and 3<=wall_thickness<=15,'Unsafe wall thickness')
    require(all(role in ROLES for role in repairs) and len(set(repairs))==len(repairs),'Unknown or duplicate repair roles')
    if action=='apply':
        require(repairs,'Choose explicit repair roles from the reviewed live inspection')
        require(not out.exists(),'Choose a fresh apply report/backup directory')
    out.mkdir(parents=True,exist_ok=True)
    inventory=inspect(u,out/'BeforeInspection.json')
    fronts={(row['front_axis'],row['front_sign']) for row in inventory['cabin_front_evidence']}
    require(len(fronts)==1,'Cabin fronts disagree; do not infer a shared collision shape')
    axis_name,front_sign=next(iter(fronts));front_axis='XY'.index(axis_name)
    actor_rows={row['name']:row for row in inventory['actors']}
    plan={'action':action,'created_utc':datetime.now(timezone.utc).isoformat(),'map':HUB,
          'repairs':list(repairs),'roles':{},'backups':[],'source_hashes':{},'saved_assets':[],
          'scope':__doc__,'deferred':['Actual walk/capsule blocking and front interaction approach check',
                                    'Packaged-game/network validation not run']}
    for role,definition in ROLES.items():
        rows=[actor_rows[name] for name in definition['actors']]
        bounds=rows[0]['local_bounds']
        require(all(row['local_bounds']==bounds for row in rows),'Role instances have different local geometry bounds')
        require(all(row['mesh'].split('.',1)[0] in (definition['source'],definition['target']) for row in rows),
                'Unexpected prop mesh assignment')
        states=[inventory['meshes'][row['mesh']] for row in rows]
        empty=all(state['simple_count']==0 and state['convex_count']==0
                  and 'COMPLEX_AS_SIMPLE' not in state['collision_complexity'] for state in states)
        shape_plan=boxes_for(role,bounds['min'],bounds['max'],front_axis,front_sign,wall_thickness)
        plan['roles'][role]={**definition,'bounds':bounds,'metadata_missing_simple_shapes':empty,
            'current_collision':states[0],'boxes':shape_plan,
            'front':{'axis':axis_name,'sign':front_sign} if role=='body' else None,
            'recommendation':'Candidate only: verify effective collision complexity / actual walk before repairing' if empty
                else 'Preserve existing geometry; run actual walk before deciding any mesh change'}
        if role in repairs and not empty:
            require(isinstance(confirmed_existing_failure,dict) and bool(confirmed_existing_failure.get(role)),
                    'Existing collision is present. Provide a concrete actual-walk failure before replacing role '+role)
    (out/'Plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    if action=='plan':
        u.log('MC_HUB_STATION_COLLISION_PLAN '+str(out/'Plan.json'));return plan

    actors={actor.get_name():actor for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()}
    lib=u.EditorAssetLibrary;mesh_editor=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
    # Read the public build settings before making any duplicate. BodySetup's
    # internal BuildScale3D is protected; do not access or bypass that field.
    for role in repairs:
        source=u.load_asset(ROLES[role]['source'])
        require(isinstance(source,u.StaticMesh),'Missing source prop mesh')
        settings=mesh_editor.get_lod_build_settings(source,0)
        scale=settings.get_editor_property('BuildScale3D')
        require(max(abs(float(value)-1.) for value in (scale.x,scale.y,scale.z))<.001,
                'Non-unit LOD build scale needs a reviewed coordinate conversion')
    # All existing packages are copied and hash-verified before the first asset mutation.
    packages=[HUB]+[definition['source'] for role,definition in ROLES.items() if role in repairs]
    packages += [ROLES[role]['target'] for role in repairs if lib.does_asset_exist(ROLES[role]['target'])]
    for package in packages:
        for suffix in ('.umap','.uasset','.uexp','.ubulk'):
            source=mesh_file(package,suffix)
            if not source.is_file():
                continue
            target=out/'BeforePackages'/source.relative_to(ROOT)
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
            sha=digest(source);require(digest(target)==sha,'Backup verification failed: '+str(source))
            plan['backups'].append({'source':str(source),'backup':str(target),'sha256':sha})
    require(any(row['source'].replace('\\','/').endswith('/Content/Maps/Hub.umap') for row in plan['backups']),
            'Hub package must be backed up before assigning copied meshes')
    for role in repairs:
        source_path=mesh_file(ROLES[role]['source']);plan['source_hashes'][str(source_path)]=digest(source_path)
    (out/'Plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

    with u.ScopedEditorTransaction('Add simple contact to existing Hub cabins and vending prop'):
        for role in repairs:
            definition=ROLES[role];source=u.load_asset(definition['source'])
            require(isinstance(source,u.StaticMesh),'Missing source prop mesh')
            if lib.does_asset_exist(definition['target']):
                target=u.load_asset(definition['target'])
                require(lib.get_metadata_tag(target,OWNER_KEY)==REVISION,'Preserve unowned collision mesh')
            else:
                target=lib.duplicate_asset(definition['source'],definition['target'])
                require(isinstance(target,u.StaticMesh),'Could not create a private collision copy')
            target.modify();body=target.get_editor_property('body_setup')
            if body is None:
                require(mesh_editor.add_simple_collisions(target,u.ScriptCollisionShapeType.BOX)>=0,'Could not initialize a private BodySetup')
                body=target.get_editor_property('body_setup')
            require(body is not None,'Private mesh has no BodySetup')
            require(body.get_outer()==target,'Collision copy unexpectedly shares its BodySetup with another asset')
            body.modify();geometry=u.KAggregateGeom();elements=[]
            for shape in plan['roles'][role]['boxes']:
                element=u.KBoxElem()
                element.set_editor_property('center',u.Vector(*shape['center']))
                element.set_editor_property('rotation',u.Rotator(0.,0.,0.))
                for name,size in zip(('x','y','z'),shape['size']):
                    element.set_editor_property(name,float(size))
                element.set_editor_property('name',u.Name(shape['name']));elements.append(element)
            geometry.set_editor_property('box_elems',elements);body.set_editor_property('agg_geom',geometry)
            body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)
            body.set_editor_property('never_needs_cooked_collision_data',False)
            sections=target.get_num_sections(0);require(sections>0,'Mesh has no render section')
            # Same project-proven editor path: PostEditChange -> StaticMesh Build
            # invalidates/recreates Chaos data. A BodySetup property assignment alone is insufficient.
            prior_section=mesh_editor.is_section_collision_enabled(target,0,0)
            mesh_editor.enable_section_collision(target,prior_section,0,0)
            require(mesh_editor.get_simple_collision_count(target)==len(elements),'Collision box count changed during the mesh rebuild')
            require(mesh_editor.get_convex_collision_count(target)==0,'Unexpected convex collision in box-only derivative')
            # Mesh rebuild rescales simple collision using its internal body
            # scale. Verify the resulting authored coordinates, not an assumed
            # protected value, before saving or assigning the private copy.
            built_boxes=list(body.get_editor_property('agg_geom').get_editor_property('box_elems'))
            for built,shape in zip(built_boxes,plan['roles'][role]['boxes']):
                center=built.get_editor_property('center')
                actual=[float(center.x),float(center.y),float(center.z)]+[
                    float(built.get_editor_property(name)) for name in ('x','y','z')]
                require(max(abs(a-b) for a,b in zip(actual,shape['center']+shape['size']))<.01,
                        'Rebuild changed collision-box coordinates; preserve private copy for inspection')
            require(target.get_num_triangles(0)==source.get_num_triangles(0),'Art geometry triangle count changed')
            require(same_mesh_bounds(target,source),'Art mesh bounds changed by more than .001 cm')
            require([target.get_material(index) for index in range(target.get_num_sections(0))]
                    ==[source.get_material(index) for index in range(source.get_num_sections(0))], 'Source materials changed')
            lib.set_metadata_tag(target,OWNER_KEY,REVISION)
            lib.set_metadata_tag(target,'MC_HubCollisionSource',source.get_path_name())
            require(lib.save_loaded_asset(target,only_if_is_dirty=False),'Could not save private collision mesh')
            plan['saved_assets'].append(definition['target'])
            for actor_name in definition['actors']:
                actor=actors[actor_name];component=actor.get_component_by_class(u.StaticMeshComponent)
                actor.modify();component.modify();component.set_static_mesh(target)
                component.set_collision_profile_name('BlockAll')
                component.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS)
                component.set_collision_response_to_channel(u.CollisionChannel.ECC_PAWN,u.CollisionResponseType.ECR_BLOCK)
                component.set_editor_property('generate_overlap_events',False)
                component.set_editor_property('can_character_step_up_on',u.CanBeCharacterBase.ECB_NO)
                actor.set_actor_enable_collision(True)
    for name,row in actor_rows.items():
        actor=actors[name];rotation=actor.get_actor_rotation()
        require([float(value) for value in actor.get_actor_location().to_tuple()]==row['location']
                and [float(rotation.pitch),float(rotation.yaw),float(rotation.roll)]==row['rotation'],
                'Prop transform changed during collision authoring')
    require(u.EditorLoadingAndSavingUtils.save_map(world,HUB),'Could not save exact Hub map')
    plan['saved_assets'].append(HUB)
    require(all(digest(Path(path))==sha for path,sha in plan['source_hashes'].items()),'An original artist mesh file changed')
    require(not dirty(u),'Unexpected unsaved packages remain; preserve for review')
    plan['after']=inspect(u,out/'AfterInspection.json')
    plan['status']='authored; actual walk validation pending'
    (out/'Report.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    u.log('MC_HUB_STATION_COLLISION_AUTHORED '+str(out/'Report.json'))
    return plan


if __name__=='__main__':
    run()
