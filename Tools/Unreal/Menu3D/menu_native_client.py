"""Task-scoped Epic MCP helpers; never operates on levels or PIE."""
from pathlib import Path
import json
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_mcp_client import NativeMCP

BASE = '/Game/UI/MainMenu3D'
UMG = 'UMGToolSet.UMGToolSet'
BP = 'editor_toolset.toolsets.blueprint.BlueprintTools'
OBJ = 'editor_toolset.toolsets.object.ObjectTools'
ASSET = 'editor_toolset.toolsets.asset.AssetTools'
CLIENT = NativeMCP()


def ref(path):
    return {'refPath': path}


def call(toolset, tool_name, **args):
    result = CLIENT.tool('call_tool', dict(toolset_name=toolset, tool_name=tool_name, arguments=args))
    if result.get('isError'):
        raise RuntimeError(result)
    values = json.loads(result['content'][0]['text'])
    if values.get('error'):
        raise RuntimeError(values)
    return values.get('returnValue')


def props(obj, values):
    schema = json.loads(call(OBJ, 'list_properties', instance=obj))
    unknown = set(values)-set(schema)
    if unknown:
        raise RuntimeError((obj, unknown))
    before = json.loads(call(OBJ, 'get_properties', instance=obj, properties=list(values)))
    def merge(old, new):
        if isinstance(old, dict) and isinstance(new, dict):
            return dict(old, **{k: merge(old.get(k), v) for k, v in new.items()})
        return new
    if not call(OBJ, 'set_properties', instance=obj, values=json.dumps(merge(before, values), ensure_ascii=False)):
        raise RuntimeError((obj, values))


def task_asset(name):
    path = BASE + '/' + name
    return ref(path + '.' + name)
