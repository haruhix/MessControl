"""Check that canonical catalog refreshes preserve artist VFX assignments; no UE required."""
import ast
from pathlib import Path

source=Path(__file__).resolve().parents[1]/'author_current_passives.py'
tree=ast.parse(source.read_text(encoding='utf-8-sig'),filename=str(source))
helper=next(node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name=='vfx_profile_for')
module=ast.fix_missing_locations(ast.Module(body=[helper],type_ignores=[]))
scope={'perks':{'Active_Rocket':{'VFXProfile':'/Game/Tests/DA_ArtistRocket.DA_ArtistRocket'}}}
exec(compile(module,str(source),'exec'),scope)
resolve=scope['vfx_profile_for']
assert resolve('Active_Rocket',{})=='/Game/Tests/DA_ArtistRocket.DA_ArtistRocket'
assert resolve('Active_Rocket',{'VFXProfile':'/Game/Tests/DA_New.DA_New'})=='/Game/Tests/DA_New.DA_New'
assert resolve('Active_Rocket',{'VFXProfile':'None'})=='None'
assert resolve('New_Perk',{})=='None'
try:
    resolve('Active_Rocket',{'VFXProfile':7})
except AssertionError:
    pass
else:
    raise AssertionError('Invalid profile assignment was silently accepted')
print('VFX profile catalog preservation: 5 cases passed')
