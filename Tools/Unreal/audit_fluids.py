"""Read the arena, liquid profile and shader nodes through editor Python."""
import json
from pathlib import Path
import unreal as u
lib=u.EditorAssetLibrary
report={}
for path,props in {
    '/Game/Data/DA_Day01':['arena_center','arena_half_size','flood_height'],
    '/Game/Data/DA_CoffeeWater':['settings'],
}.items():
    obj=lib.load_asset(path)
    report[path]={p:str(obj.get_editor_property(p)) for p in props}
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
report['arena']=[dict(name=a.get_actor_label(),position=str(a.get_actor_location()),bounds=str(a.get_actor_bounds(False))) for a in actors if isinstance(a,(u.MCTongue,u.MCThroat)) or any(str(t)=='MCCameraBounds' for t in a.tags)]
report['customs']={}
for path in ['/Game/Gameplay/Liquid/M_CoffeePuddle','/Game/Art/Materials/M_CoffeeSurface']:
    mat=lib.load_asset(path)
    report['customs'][path]=[dict(description=n.get_editor_property('description'),code=n.get_editor_property('code')) for n in u.MaterialEditingLibrary.get_material_expressions(mat) if isinstance(n,u.MaterialExpressionCustom)]
Path(u.Paths.project_saved_dir(),'FluidAudit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.log('MC_FLUID_AUDIT_PASS')
