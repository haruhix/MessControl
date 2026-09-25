"""Read-only checks against the saved artist arena, including physical floor traces."""
import json
from pathlib import Path
import unreal as u

levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
if not levels.load_level('/Game/Maps/L_Mouth'): raise RuntimeError('Map load failed')
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
slots=[a for a in actors if isinstance(a,u.MCArenaToothSocket)]
decor=[a for a in actors if a.get_actor_label().startswith('ART | Far tooth')]
if sorted(a.tooth_id for a in slots)!=list(range(1,9)) or len(decor)!=2: raise RuntimeError('Expected 8 gameplay + 2 decorative teeth')
for side in (-1,1):
    row=[a for a in slots if a.get_actor_location().y*side>0]
    far=next(a for a in decor if a.get_actor_location().y*side>0)
    if len(row)!=4 or any(a.get_actor_location().x>=far.get_actor_location().x for a in row): raise RuntimeError('Wrong front/far mapping')
if any(a.get_actor_label() in ('SM_Location_teeth','SM_Location_teeth2','COLLISION | Tongue floor') for a in actors): raise RuntimeError('Legacy rows/floor remain')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
probes=[]
for x,expected in [(-900,-126),(-500,-67),(0,-36),(600,-48),(1200,-126)]:
    hit=u.SystemLibrary.line_trace_single(world,u.Vector(x,0,300),u.Vector(x,0,-400),u.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],u.DrawDebugTrace.NONE,True)
    if hit is None: raise RuntimeError('No physical floor at '+str(x))
    parts=hit.to_tuple()
    if not parts[0]: raise RuntimeError('No blocking floor at '+str(x))
    point=parts[5]
    if abs(point.z-expected)>4: raise RuntimeError('Wrong floor at '+str(x)+': '+str(point))
    probes.append(dict(x=x,z=point.z))
markers=[a for a in actors if isinstance(a,u.MCFoodDisposal)]
if len(markers)!=2 or sum(bool(a.get_editor_property('brush_bin')) for a in markers)!=1: raise RuntimeError('Expected separate food and brush exits')
profile=u.load_asset('/Game/Data/DA_ArenaTooth')
if not profile.get_editor_property('gameplay_material'): raise RuntimeError('Gameplay enamel missing')
report=dict(gameplay_teeth=8,decorative_teeth=2,floor_probes=probes,exits=2)
(Path(u.Paths.project_saved_dir())/'ArenaVerification.json').write_text(json.dumps(report,indent=2))
u.log('MC_ARTIST_ARENA_VERIFIED '+json.dumps(report))
u.SystemLibrary.quit_editor()
