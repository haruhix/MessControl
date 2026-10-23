"""Bake L_Mouth's existing BeginPlay throat lining as passive menu geometry."""
import unreal as u,re,json
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/MenuGameLocation20261023'
es=u.get_editor_subsystem(u.EditorActorSubsystem);levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
assert u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world().get_path_name()=='/Game/Maps/L_MainMenu.L_MainMenu'
folder='/Game/UI/MenuShowcase/GameLocation20261023'
source=u.load_asset('/Game/Maps/L_Mouth');throat=next(a for a in u.ObjectIterator(u.MCThroat) if a.get_path_name().startswith(source.get_path_name()+':'))
code=(ROOT/'Source/MessControl/Private/MCThroat.cpp').read_text(encoding='utf8')
def vectors(name):
    section=re.search(r'const FVector '+name+r'\[\]\s*=\s*\{(.*?)\};',code,re.S).group(1)
    return [u.Vector(*[float(s) for s in row.split(',')]) for row in re.findall(r'\{([^{}]+)\}',section)]
basis=vectors('FrontApertureBasis');opened=vectors('FrontApertureOpen')
amount=1.-float(throat.get_editor_property('manual_authored_expansion'))
boundary=[a+(b-a)*amount for a,b in zip(basis,opened)]
center=u.Vector(.00003081,339.24208692,-386.35745988)+(u.Vector(.00001988,267.42235677,-422.93604172)-u.Vector(.00003081,339.24208692,-386.35745988))*amount
normal=u.Vector(0,.9747185974,.2234360218);segments=len(boundary)
vertices=[];normals=[];uv=[];colors=[];triangles=[]
for ring in range(7):
    t=ring/6;scale=1.025*(1-t)
    for i in range(segments+1):
        vertices.append(center+(boundary[i%segments]-center)*scale+normal*(12+t*400))
        normals.append(-normal);uv.append(u.Vector2D(i/segments,t))
        colors.append(u.MathLibrary.linear_color_lerp_using_hsv(u.LinearColor(.025,.003,.006,1),u.LinearColor(.001,.0001,.0002,1),t))
        if ring>0 and i>0:
            b=(ring-1)*(segments+1)+i-1;c=b+segments+1
            triangles.extend([u.IntVector(b,c,b+1),u.IntVector(b+1,c,c+1)])
buffers=u.GeometryScriptSimpleMeshBuffers(vertices=vertices,normals=normals,uv0=uv,vertex_colors=colors,triangles=triangles)
dynamic=u.new_object(u.DynamicMesh);u.GeometryScript_MeshEdits.append_buffers_to_mesh(dynamic,buffers)
mesh=u.load_asset(folder+'/SM_ThroatDepth')
if not mesh:
    options=u.GeometryScriptCreateNewStaticMeshAssetOptions(enable_collision=False,enable_nanite=False)
    result=u.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dynamic,folder+'/SM_ThroatDepth',options)
    mesh=result[0] if isinstance(result,tuple) else result
assert isinstance(mesh,u.StaticMesh)
material=u.load_asset(folder+'/MI_ThroatDepth')
if not material:
    material=u.AssetToolsHelpers.get_asset_tools().create_asset('MI_ThroatDepth',folder,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    u.MaterialEditingLibrary.set_material_instance_parent(material,u.load_asset('/Game/Gameplay/VFX/M_ReactionSoft'))
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(material,'Color',u.LinearColor(1,1,1,1))
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(material,'Opacity',1.)
    u.MaterialEditingLibrary.update_material_instance(material)
mesh.set_material(0,material)
assert u.EditorAssetLibrary.save_loaded_asset(material);assert u.EditorAssetLibrary.save_loaded_asset(mesh)
actors=es.get_all_level_actors();mouth=next(a for a in actors if a.get_actor_label()=='MENU | Throat | fixed game pose')
depth=next((a for a in actors if a.get_actor_label()=='MENU | Throat depth'),None)
if not depth:depth=es.spawn_actor_from_class(u.StaticMeshActor,u.Vector())
depth.set_actor_label('MENU | Throat depth');depth.tags=['MC_MenuGameLocation'];depth.set_folder_path('Menu/GameLocation')
c=depth.static_mesh_component;c.set_mobility(u.ComponentMobility.MOVABLE);c.set_static_mesh(mesh);c.set_material(0,material);c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION);c.set_cast_shadow(False)
depth.set_actor_transform(mouth.get_actor_transform(),False,True);depth.set_actor_enable_collision(False);depth.set_actor_tick_enabled(False)
hero=next(a for a in actors if 'MC_MenuV2_Hero' in [str(t) for t in a.tags]);camera=next(a for a in actors if 'MC_MenuV2_Camera' in [str(t) for t in a.tags])
light=next((a for a in actors if a.get_actor_label()=='MENU | Portrait fill'),None)
if not light:light=es.spawn_actor_from_class(u.RectLight,u.Vector())
light.set_actor_label('MENU | Portrait fill');light.tags=['MC_MenuPortraitLight'];light.set_folder_path('Menu/Presentation')
light.set_actor_location(camera.get_actor_location()+u.Vector(80,-40,90),False,True)
light.set_actor_rotation(u.MathLibrary.find_look_at_rotation(light.get_actor_location(),hero.get_actor_location()+u.Vector(0,0,55)),False)
l=light.get_component_by_class(u.RectLightComponent);l.set_editor_property('intensity_units',u.LightUnits.LUMENS);l.set_intensity(20.);l.set_light_color(u.LinearColor(.95,.97,1.,1),False);l.set_attenuation_radius(800.)
l.set_editor_property('source_width',180.);l.set_editor_property('source_height',180.);l.set_cast_shadows(True)
assert levels.save_current_level()
report=json.loads((OUT/'Integration.json').read_text());report.update(passive_throat_lining={'mesh':mesh.get_path_name(),'material':material.get_path_name(),'source':'AMCThroat::BeginPlay ThroatDepth, same aperture/rings/vertex colors','triangles':len(triangles)},portrait_fill={'actor':light.get_path_name(),'lumens':20})
(OUT/'Integration.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.AutomationLibrary.take_high_res_screenshot(1600,900,str(OUT/'PreviewDepth.png'),camera=camera,force_game_view=True)
print('PASSIVE_THROAT_AND_MENU_LIGHT_SAVED')
