"""Import/reimport original suction audio through Unreal's native WAV importer.

Run with UnrealEditor-Cmd <project> -run=pythonscript -script=<absolute path>.
This only writes /Game/Audio/S_FoodSuctionWind; it does not modify other sounds.
The source can be regenerated with Tools/generate_food_suction_audio.py.
"""
from pathlib import Path
import json
import unreal as u

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "ArtSource" / "Audio" / "FoodSuctionWind.wav"
ASSET = "/Game/Audio/S_FoodSuctionWind"

assert SOURCE.is_file(), f"Missing original audio source: {SOURCE}"
task = u.AssetImportTask()
task.set_editor_properties(dict(
    filename=str(SOURCE),
    destination_path="/Game/Audio",
    destination_name="S_FoodSuctionWind",
    automated=True,
    replace_existing=True,
    save=True,
    factory=u.SoundFactory(),
))
u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
sound = u.EditorAssetLibrary.load_asset(ASSET)
assert isinstance(sound, u.SoundWave), f"WAV import did not produce SoundWave: {ASSET}"
sound.set_editor_property("looping", False)
assert u.EditorAssetLibrary.save_loaded_asset(sound, only_if_is_dirty=False), ASSET
print("MC_FOOD_SUCTION_AUDIO_IMPORTED " + json.dumps(dict(
    source=str(SOURCE), asset=sound.get_path_name(),
    imported_paths=list(task.get_editor_property("imported_object_paths")),
    duration=sound.get_editor_property("duration"),
)))
