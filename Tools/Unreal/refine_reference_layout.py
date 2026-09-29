"""Reference tooth rows, raised order button and brush foam material. Run with PIE stopped."""
import unreal as u, math, json
from pathlib import Path
actors=u.get_editor_subsystem(u.EditorActorSubsystem); lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary
allactors=actors.get_all_level_actors()
mat=lib.load_asset('/Game/Art/Materials/MI_ArenaToothReference')
positions=[-510,-275,-40,195,430,655,850,1025]
report=[]
for side in (-1,1):
    sockets=sorted([a for a in allactors if isinstance(a,u.MCArenaToothSocket) and a.get_actor_location().y*side>0],key=lambda a:a.get_editor_property('tooth_id'))
    assert len(sockets)==4
    far=next(a for a in allactors if a.get_actor_label()=='ART | Far tooth '+('Left' if side<0 else 'Right'))
    for i,x in enumerate(positions):
        t=i/7; y=side*(940-200*t*t)-30
        variant=[1,2,2,3,3,4,4,5][i]
        mesh=lib.load_asset('/Game/Gameplay/Throat/Teeth/SM_ReferenceTooth_%02d%s'%(variant,'_Mirrored' if side<0 else ''))
        if i in (0,2,4,6): a=sockets[i//2]; c=a.get_editor_property('preview')
        elif i==7: a=far; c=a.static_mesh_component
        else:
            label='ART | Reference row %s %02d'%('L' if side<0 else 'R',i)
            a=next((a for a in allactors if a.get_actor_label()==label),None)
            if a is None: a=actors.spawn_actor_from_class(u.StaticMeshActor,u.Vector())
            a.set_actor_label(label); a.set_folder_path('Art/Mouth/Teeth'); c=a.static_mesh_component
            c.set_collision_enabled(u.CollisionEnabled.QUERY_AND_PHYSICS)
        c.set_static_mesh(mesh); c.set_material(0,mat)
        b=mesh.get_bounds(); width=[224,230,220,219,211,200,183,173][i]
        # +Y of the source crown follows the row; root pivots stay centred in gum.
        sx=(174-15*t)/(b.box_extent.x*2); sy=width/(b.box_extent.y*2); sz=(345-25*t)/(b.box_extent.z*2)
        a.set_actor_scale3d(u.Vector(sx,sy,sz))
        yaw=90-side*math.degrees(math.atan(400*t/1535))
        a.set_actor_rotation(u.Rotator(pitch=0,yaw=yaw,roll=0),False)
        a.set_actor_location(u.Vector(x,y,20+13*math.sin(t*math.pi)),False,False)
        report.append({'actor':a.get_actor_label(),'position':[x,y,20+13*math.sin(t*math.pi)],'variant':variant})
for a in allactors:
    if isinstance(a,u.MCThroat):
        a.set_editor_property('uvula_top',u.Vector(-120,0,430)); a.set_editor_property('uvula_length',190.0)
        a.set_editor_property('anticipation_seconds',3.0); a.call_method('RebuildAppearance')
name='M_BrushFoam'; path='/Game/Gameplay/Care/'+name
if not lib.does_asset_exist(path):
    m=u.AssetToolsHelpers.get_asset_tools().create_asset(name,'/Game/Gameplay/Care',u.Material,u.MaterialFactoryNew())
    def constant(value,prop):
        n=edit.create_material_expression(m,u.MaterialExpressionConstant); n.set_editor_property('r',value); edit.connect_material_property(n,'',prop)
    n=edit.create_material_expression(m,u.MaterialExpressionConstant3Vector); n.set_editor_property('constant',u.LinearColor(.87,.96,1,1)); edit.connect_material_property(n,'',u.MaterialProperty.MP_BASE_COLOR)
    constant(.24,u.MaterialProperty.MP_ROUGHNESS); constant(.65,u.MaterialProperty.MP_SPECULAR)
    edit.recompile_material(m); lib.save_loaded_asset(m,False)
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
Path(u.Paths.project_saved_dir(),'ReferenceToothLayout.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_REFERENCE_LAYOUT_FINISHED')
