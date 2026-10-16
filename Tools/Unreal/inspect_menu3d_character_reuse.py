"""Read current character/animation references; never mutate editor objects."""
import json
from pathlib import Path
import unreal as u

OUT = Path(u.Paths.project_dir()).resolve() / 'ArtSource/UI/MainMenu3D/20261015_refB_v1/Assembly'
OUT.mkdir(parents=True, exist_ok=True)

def ref(value):
    return value.get_path_name() if isinstance(value, u.Object) else str(value)

def props(path, names):
    ob = u.EditorAssetLibrary.load_asset(path)
    result = {'asset': path, 'loaded': bool(ob)}
    if ob:
        for name in names:
            try:
                result[name] = ref(ob.get_editor_property(name))
            except Exception as exc:
                result[name] = {'error': str(exc)}
    return result

report = {
    'scope': 'Live read-only UObject references; no save, spawn, level change or PIE.',
    'appearance': props('/Game/Data/DA_PlayerAppearance', ['skeletal_mesh', 'material', 'physics_asset', 'brush_mesh', 'mesh_transform', 'brush_transform', 'bone_map']),
    'animation_profile': props('/Game/Data/DA_ToothAnimation', ['grab_left', 'grab_right', 'push', 'tired']),
    'emotes': [],
}
appearance = u.EditorAssetLibrary.load_asset('/Game/Data/DA_PlayerAppearance')
mesh = appearance.get_editor_property('skeletal_mesh') if appearance else None
if mesh:
    report['active_mesh'] = {'asset': mesh.get_path_name(), 'skeleton': ref(mesh.get_editor_property('skeleton')), 'materials': [ref(s.material_interface) for s in mesh.get_editor_property('materials')]}
emotes = u.EditorAssetLibrary.load_asset('/Game/Data/DA_Emotes')
if emotes:
    for entry in emotes.get_editor_property('entries'):
        clip = entry.get_editor_property('animation')
        report['emotes'].append({'id': str(entry.id), 'label': str(entry.label), 'clip': ref(clip), 'seconds': clip.get_play_length() if clip else None})
report['animation_assets'] = u.EditorAssetLibrary.list_assets('/Game/Gameplay/Character/Animations', recursive=True, include_folder=False)
(OUT / 'ue_character_reuse_live.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('MC_MENU3D_CHARACTER_REUSE ' + json.dumps(report, ensure_ascii=False))
