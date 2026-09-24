"""Tools > Execute Python Script: copy F1 widget's saved local tuning into the shared DA."""
import unreal
import configparser
from pathlib import Path
path=Path(unreal.Paths.project_saved_dir())/"AnimationTuning.ini"
if not path.exists(): raise RuntimeError("Save a preset in the F1 Animation Lab first.")
config=configparser.ConfigParser(); config.read(path)
asset=unreal.EditorAssetLibrary.load_asset("/Game/Data/DA_ToothAnimation")
settings=asset.get_editor_property("settings")
for key,prop in {"Squash":"squash","Stretch":"stretch","Bob":"bob","Lean":"lean","FollowThrough":"follow_through","Tempo":"tempo","Anticipation":"anticipation","Exaggeration":"exaggeration"}.items():
    settings.set_editor_property(prop,config.getfloat("ToothAnimation",key))
asset.set_editor_property("settings",settings)
unreal.EditorAssetLibrary.save_loaded_asset(asset)
unreal.log("Animation Lab preset applied to DA_ToothAnimation. Commit the asset to share it.")
