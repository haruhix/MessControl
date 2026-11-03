import json
from pathlib import Path
import unreal as u

source=Path(u.Paths.project_dir()).resolve()/'ArtSource/CharacterAnimation/ToothHits_20261102/WeaponReference.json'
data=json.loads(source.read_text())
assets={'hit_combo':'/Game/Gameplay/CharacterCurrent/Animations/ToothHits/A_Teeth_Hit_Combo',
        'hit3':'/Game/Gameplay/CharacterCurrent/Animations/ToothHits/A_Teeth_Hit3'}
fields={'MCWeaponX':('location',0),'MCWeaponY':('location',1),'MCWeaponZ':('location',2),
        'MCWeaponQX':('quaternion',0),'MCWeaponQY':('quaternion',1),'MCWeaponQZ':('quaternion',2),'MCWeaponQW':('quaternion',3),
        'MCWeaponSX':('scale',0),'MCWeaponSY':('scale',1),'MCWeaponSZ':('scale',2)}
report=[]
for name,path in assets.items():
    clip=u.EditorAssetLibrary.load_asset(path)
    assert isinstance(clip,u.AnimSequence),path
    rows=data['clip'][name];times=[(r['frame']-1)/24 for r in rows]
    for curve in ['MCWeaponAuthored',*fields]:
        if u.AnimationLibrary.does_curve_exist(clip,curve,u.RawCurveTrackTypes.RCT_FLOAT):
            u.AnimationLibrary.remove_curve(clip,curve,False)
        u.AnimationLibrary.add_curve(clip,curve,u.RawCurveTrackTypes.RCT_FLOAT,False)
        values=[1.0]*len(rows) if curve=='MCWeaponAuthored' else [r['relative'][fields[curve][0]][fields[curve][1]] for r in rows]
        u.AnimationLibrary.add_float_curve_keys(clip,curve,times,values)
        assert u.AnimationLibrary.does_curve_exist(clip,curve,u.RawCurveTrackTypes.RCT_FLOAT),curve
    assert u.EditorAssetLibrary.save_loaded_asset(clip,only_if_is_dirty=False)
    report.append({'clip':clip.get_path_name(),'frames':len(rows),'curves':list(fields)+['MCWeaponAuthored']})
print('TOOTH_WEAPON_CURVES_SAVED',json.dumps(report))

