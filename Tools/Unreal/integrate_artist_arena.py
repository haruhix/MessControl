"""Adapt the September artist arena without changing its source meshes/materials.
Run once after export_arena_for_gameplay.py and Blender/prepare_arena_teeth.py.
Subsequent runs validate/preserve authored placements. Use a new migration for new art.
"""
import json, math
from pathlib import Path
import unreal as u

root=Path(u.Paths.project_dir()).resolve()
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
meshes=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
folder='/Game/Gameplay/Arena'
tag='ArtistArenaIntegrationV1'

def save(asset):
    if not lib.save_loaded_asset(asset,only_if_is_dirty=False): raise RuntimeError('Save failed: '+asset.get_path_name())

def link(a,b,pin='',out=''):
    if not edit.connect_material_expressions(a,out,b,pin): raise RuntimeError('Material connection: '+pin)

def gameplay_material():
    path=folder+'/M_ArenaToothArt'
    if lib.does_asset_exist(path): return lib.load_asset(path)
    mat=lib.duplicate_asset('/Game/Art/Materials/M_Enamel',path)
    if not mat: raise RuntimeError('Missing artist enamel')
    base=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_BASE_COLOR)
    output=edit.get_material_property_input_node_output_name(mat,u.MaterialProperty.MP_BASE_COLOR)
    if not base: raise RuntimeError('Artist enamel requires a Base Color input')
    def node(cls,x,y): return edit.create_material_expression(mat,cls,x,y)
    pos=node(u.MaterialExpressionPreSkinnedPosition,0,1000)
    interp=node(u.MaterialExpressionVertexInterpolator,220,1000); link(pos,interp)
    custom=node(u.MaterialExpressionCustom,750,1000)
    custom.set_editor_property('description','Care states over artist enamel; all other shading is preserved')
    custom.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs=[]
    for name in ('Enamel','P','Coffee','Damage','Flash'):
        entry=u.CustomInput(); entry.set_editor_property('input_name',name); inputs.append(entry)
    custom.set_editor_property('inputs',inputs)
    custom.set_editor_property('code','''
float waves=sin(P.y*.045+sin(P.z*.034)*1.8)+sin(P.x*.055+P.z*.038)+.55*cos(P.z*.07-P.y*.04);
float stain=smoothstep(.05,.35,waves)*saturate(Coffee);
float3 color=lerp(Enamel,float3(.18,.052,.008)*(.8+.2*sin(P.z*.08)),stain);
float crack=1-smoothstep(.025,.075,abs(sin(P.z*.044+P.y*.029+sin(P.y*.055)*.8)));
crack*=smoothstep(.15,.65,Damage)*smoothstep(-20,50,P.z);
color=lerp(color,float3(.075,.024,.012),crack*.85);
return lerp(color,float3(1,.3,.08),saturate(Flash)*.55);
''')
    link(base,custom,'Enamel',output); link(interp,custom,'P')
    for i,(name,pin) in enumerate((('Coffee','Coffee'),('Damage','Damage'),('HitFlash','Flash'))):
        p=node(u.MaterialExpressionScalarParameter,400,1200+i*120)
        p.set_editor_property('parameter_name',name); p.set_editor_property('default_value',0)
        p.set_editor_property('group','Gameplay'); link(p,custom,pin)
    if not edit.connect_material_property(custom,'',u.MaterialProperty.MP_BASE_COLOR): raise RuntimeError('Base color output')
    errors=edit.recompile_material(mat)
    if errors: raise RuntimeError(str(errors))
    save(mat); return mat

if not levels.load_level('/Game/Maps/L_Mouth'): raise RuntimeError('Missing map')
all_actors=actors.get_all_level_actors()
existing=[a for a in all_actors if tag in [str(t) for t in a.tags]]
if existing:
    sockets=[a for a in existing if isinstance(a,u.MCArenaToothSocket)]
    decor=[a for a in existing if a.get_actor_label().startswith('ART | Far tooth')]
    if sorted(a.tooth_id for a in sockets)!=list(range(1,9)) or len(decor)!=2:
        raise RuntimeError('Partly changed integration; inspect placements before migrating again')
    for a in existing:
        if a.get_actor_label()=='COLLISION | Artist mouth': a.static_mesh_component.set_visibility(False)
    if not levels.save_current_level(): raise RuntimeError('Map save failed')
    u.log('MC_ARTIST_ARENA_PRESERVED: 8 gameplay teeth, 2 decorative')
    u.SystemLibrary.quit_editor()
else:
    rows=[a for a in all_actors if a.get_actor_label() in ('SM_Location_teeth','SM_Location_teeth2')]
    shell=next((a for a in all_actors if a.get_actor_label()=='SM_Location2'),None)
    if len(rows)!=2 or not shell: raise RuntimeError('Expected two artist rows and the artist mouth')
    manifest=json.loads((root/'ArtSource/ArenaGameplay/teeth.json').read_text())
    u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
    for item in manifest:
        for key in ('mesh','mirror'):
            name=item[key]; path=folder+'/'+name
            if not lib.does_asset_exist(path):
                task=u.AssetImportTask(); task.filename=str(root/'ArtSource/ArenaGameplay'/(name+'.fbx'))
                task.destination_path=folder; task.destination_name=name; task.automated=True; task.save=True
                opt=u.FbxImportUI(); opt.import_mesh=True; opt.import_as_skeletal=False
                opt.import_materials=False; opt.import_textures=False; opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
                opt.static_mesh_import_data.combine_meshes=True; opt.static_mesh_import_data.auto_generate_collision=False
                opt.static_mesh_import_data.generate_lightmap_u_vs=False
                task.options=opt; task.factory=u.FbxFactory(); assets.import_asset_tasks([task])
            mesh=lib.load_asset(path)
            if not mesh: raise RuntimeError('Import failed: '+path)
            bounds=mesh.get_bounds()
            if not (50<bounds.box_extent.z<300): raise RuntimeError('Wrong tooth import scale: '+str(bounds))
            mesh.set_material(0,lib.load_asset('/Game/Art/Materials/M_Enamel'))
            if meshes.get_simple_collision_count(mesh)==0: meshes.add_simple_collisions(mesh,u.ScriptingCollisionShapeType.BOX)
            save(mesh)
    material=gameplay_material()
    profile=lib.load_asset('/Game/Data/DA_ArenaTooth'); profile.set_editor_property('gameplay_material',material); save(profile)
    layout=[]
    for side,row in enumerate(sorted(rows,key=lambda a:a.get_actor_location().y)):
        p=row.get_actor_location(); r=row.get_actor_rotation(); s=row.get_actor_scale3d()
        c=math.cos(math.radians(r.yaw)); sn=math.sin(math.radians(r.yaw))
        placed=[]
        for item in manifest:
            x,y,z=item['centre_ue']; x*=s.x; y*=s.y; z*=s.z
            placed.append((u.Vector(p.x+c*x-sn*y,p.y+sn*x+c*y,p.z+z),item))
        placed.sort(key=lambda t:t[0].x)
        for depth,(centre,item) in enumerate(placed):
            mesh=lib.load_asset(folder+'/'+item['mirror' if s.x<0 else 'mesh'])
            if depth<4:
                number=depth*2+side+1
                actor=actors.spawn_actor_from_class(u.MCArenaToothSocket,centre,r)
                actor.set_editor_property('tooth_id',number); actor.preview.set_static_mesh(mesh)
                actor.preview.set_material(0,material); actor.set_actor_label('ARENA | Tooth %02d'%number)
                actor.set_folder_path('Gameplay/ArenaTeeth')
            else:
                actor=actors.spawn_actor_from_class(u.StaticMeshActor,centre,r)
                actor.static_mesh_component.set_static_mesh(mesh)
                actor.static_mesh_component.set_collision_profile_name('BlockAll')
                actor.set_actor_label('ART | Far tooth '+('Left' if side==0 else 'Right'))
                actor.set_folder_path('Art/ArenaDecoration')
            actor.set_editor_property('tags',[tag])
            layout.append(dict(label=actor.get_actor_label(),mesh=mesh.get_path_name(),location=[centre.x,centre.y,centre.z],yaw=r.yaw,source_row=row.get_actor_label()))
    collision_path=folder+'/SM_MouthCollision'
    collision=lib.load_asset(collision_path) if lib.does_asset_exist(collision_path) else lib.duplicate_asset(shell.static_mesh_component.static_mesh.get_path_name(),collision_path)
    body=collision.get_editor_property('body_setup')
    body.set_editor_property('collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    body.set_editor_property('double_sided_geometry',True); save(collision)
    proxy=actors.spawn_actor_from_class(u.StaticMeshActor,shell.get_actor_location(),shell.get_actor_rotation())
    proxy.set_actor_scale3d(shell.get_actor_scale3d()); proxy.static_mesh_component.set_static_mesh(collision)
    proxy.static_mesh_component.set_collision_profile_name('BlockAll')
    proxy.static_mesh_component.set_hidden_in_game(True); proxy.static_mesh_component.set_cast_shadow(False)
    proxy.static_mesh_component.set_visibility(False)
    proxy.set_actor_hidden_in_game(True); proxy.set_actor_label('COLLISION | Artist mouth')
    proxy.set_folder_path('Gameplay/Collision'); proxy.set_editor_property('tags',[tag])
    for actor in all_actors:
        label=actor.get_actor_label()
        if actor in rows or isinstance(actor,u.MCArenaToothSocket) or label=='COLLISION | Tongue floor':
            actors.destroy_actor(actor)
        elif label in ('COLLISION | Left boundary','COLLISION | Right boundary','COLLISION | Front boundary'):
            p=actor.get_actor_location(); s=actor.get_actor_scale3d()
            actor.set_actor_location(u.Vector(-1150 if 'Front' in label else p.x,p.y,100),False,False)
            actor.set_actor_scale3d(u.Vector(s.x,s.y,8))
    for label,pos,extent,brush in [('GAMEPLAY | Throat',u.Vector(1300,-100,-40),u.Vector(135,280,300),False),('GAMEPLAY | Brush exit',u.Vector(-1260,-100,0),u.Vector(90,650,300),True)]:
        marker=actors.spawn_actor_from_class(u.MCFoodDisposal,pos,u.Rotator())
        marker.set_editor_property('brush_bin',brush); marker.get_editor_property('volume').set_box_extent(extent)
        marker.set_actor_label(label); marker.set_folder_path('Gameplay/Exits'); marker.set_editor_property('tags',[tag])
    plan=lib.load_asset('/Game/Data/DA_Day01')
    plan.set_editor_property('arena_center',u.Vector(150,-100,0))
    plan.set_editor_property('arena_half_size',u.Vector(1250,820,300)); save(plan)
    water=lib.load_asset('/Game/Data/DA_CoffeeWater'); tuning=water.get_editor_property('settings')
    tuning.set_editor_property('dry_height',-200); tuning.set_editor_property('drain_point',u.Vector(1300,-100,-40))
    water.set_editor_property('settings',tuning); save(water)
    if not levels.save_current_level(): raise RuntimeError('Map save failed')
    (root/'ArtSource/ArenaGameplay/layout.json').write_text(json.dumps(layout,indent=2),encoding='utf-8')
    u.log('MC_ARTIST_ARENA_PASS: 8 gameplay teeth + 2 decorative; artist collision; authored exits')
    u.SystemLibrary.quit_editor()
