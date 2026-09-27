"""Read-only checks of pressure WPO and the saved arena's physical contact.

Compare simple queries (characters/rigid bodies) to triangle queries at the
current artist transforms. Do not pin floor heights to an obsolete mesh.
"""
import json
from pathlib import Path
import unreal as u

levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
if not levels.load_level('/Game/Maps/L_Mouth'): raise RuntimeError('Map load failed')
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
tongues=[a for a in actors if isinstance(a,u.MCTongue)]
if len(tongues)!=1: raise RuntimeError('Expected one deforming tongue')
collision=next(a for a in actors if a.get_actor_label()=='COLLISION | Artist mouth')
if collision.static_mesh_component.get_collision_enabled()!=u.CollisionEnabled.NO_COLLISION:
    raise RuntimeError('Hidden old mouth shell still blocks the new artist geometry')
slots=[a for a in actors if isinstance(a,u.MCArenaToothSocket)]
decor=[a for a in actors if a.get_actor_label().startswith('ART | Far tooth')]
if sorted(a.tooth_id for a in slots)!=list(range(1,9)) or len(decor)!=2: raise RuntimeError('Expected 8 gameplay + 2 decorative teeth')
for side in (-1,1):
    row=[a for a in slots if a.get_actor_location().y*side>0]
    far=next(a for a in decor if a.get_actor_location().y*side>0)
    if len(row)!=4 or any(a.get_actor_location().x>=far.get_actor_location().x for a in row): raise RuntimeError('Wrong front/far mapping')
if any(a.get_actor_label() in ('SM_Location_teeth','SM_Location_teeth2','COLLISION | Tongue floor') for a in actors): raise RuntimeError('Legacy rows/floor remain')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
def trace(start, end, complex_trace, ignored):
    hit=u.SystemLibrary.line_trace_single(world,start,end,u.TraceTypeQuery.TRACE_TYPE_QUERY1,complex_trace,ignored,u.DrawDebugTrace.NONE,True)
    return hit.to_tuple() if hit is not None and hit.to_tuple()[0] else None

def distance(a,b):
    return ((a.x-b.x)**2+(a.y-b.y)**2+(a.z-b.z)**2)**.5

# Pressure already deforms the shared render/physics mesh. A second displacement
# in the material creates a gap invisible to CPU floor tests.
edit=u.MaterialEditingLibrary
mat=u.load_asset('/Game/Gameplay/Arena/M_TonguePain')
wpo=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
assert wpo is None, 'Material displaces the tongue away from its physical geometry'
assert tongues[0].surface_material==u.load_asset('/Game/Gameplay/Arena/MI_TonguePain')
assert tongues[0].surface_material.parent==mat

probes=[]
for x in (-900,-600,-300,0,300,600,900,1200):
    for y in (-450,0,450):
        start,end=u.Vector(x,y,400),u.Vector(x,y,-700)
        surface=trace(start,end,True,[a for a in actors if a!=tongues[0]])
        assert surface, 'No tongue geometry at '+str((x,y))
        floor=trace(u.Vector(x,y,surface[5].z+80),end,False,[])
        assert floor and floor[9]==tongues[0], 'Unexpected floor collider at '+str((x,y))
        assert distance(surface[5],floor[5])<.1, 'Floor does not match tongue geometry'
        probes.append(dict(x=x,y=y,z=floor[5].z))

# Sample each new static mesh from both sides along all three axes. An empty
# simple collision setup used to pass triangle traces but fail gameplay queries.
wall_probes={}
for name in ('SM_Gum','SM_Hole','SM_Wall_01'):
    mesh=u.load_asset('/Game/Art/Meshes/Arena/'+name)
    assert mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE, name
    actor=next(a for a in actors if isinstance(a,u.StaticMeshActor) and a.static_mesh_component.static_mesh==mesh)
    assert actor.static_mesh_component.get_collision_enabled()==u.CollisionEnabled.QUERY_AND_PHYSICS
    origin,extent=actor.get_actor_bounds(False)
    center=[origin.x,origin.y,origin.z]; half=[extent.x,extent.y,extent.z]
    ignored=[a for a in actors if a!=actor]
    count,sweeps,max_error=0,0,0
    for axis in range(3):
        side_axes=[i for i in range(3) if i!=axis]
        for u_value in (-.8,-.4,0,.4,.8):
            for v_value in (-.8,-.4,0,.4,.8):
                for direction in (-1,1):
                    a=center.copy(); b=center.copy()
                    for side,value in zip(side_axes,(u_value,v_value)):
                        a[side]=b[side]=center[side]+half[side]*value
                    a[axis]-=direction*(half[axis]+150); b[axis]+=direction*(half[axis]+150)
                    start,end=u.Vector(*a),u.Vector(*b)
                    visual=trace(start,end,True,ignored)
                    physical=trace(start,end,False,ignored)
                    assert bool(visual)==bool(physical), 'Missing or phantom collision: '+name
                    if not visual: continue
                    error=distance(visual[5],physical[5]); max_error=max(error,max_error)
                    assert error<.1, 'Collider differs from visible triangles: '+name
                    count+=1
                    sweep=u.SystemLibrary.capsule_trace_single(world,start,end,28,58,u.TraceTypeQuery.TRACE_TYPE_QUERY1,False,ignored,u.DrawDebugTrace.NONE,True)
                    assert sweep and sweep.to_tuple()[0] and sweep.to_tuple()[9]==actor, 'Character capsule passes through '+name
                    sweeps+=1
    assert count>=10, 'Insufficient collision samples: '+name
    wall_probes[name]=dict(line_hits=count,capsule_hits=sweeps,max_error_cm=max_error)
markers=[a for a in actors if isinstance(a,u.MCFoodDisposal)]
if len(markers)!=2 or sum(bool(a.get_editor_property('brush_bin')) for a in markers)!=1: raise RuntimeError('Expected separate food and brush exits')
profile=u.load_asset('/Game/Data/DA_ArenaTooth')
if not profile.get_editor_property('gameplay_material'): raise RuntimeError('Gameplay enamel missing')
report=dict(gameplay_teeth=8,decorative_teeth=2,material_displacement=False,floor_probes=probes,wall_probes=wall_probes,exits=2)
(Path(u.Paths.project_saved_dir())/'ArenaVerification.json').write_text(json.dumps(report,indent=2))
u.log('MC_ARTIST_ARENA_VERIFIED '+json.dumps(report))
u.SystemLibrary.quit_editor()
