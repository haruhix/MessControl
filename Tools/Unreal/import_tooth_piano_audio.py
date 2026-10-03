"""Import piano octave samples; transposition remains inside Unreal's pitch limits."""
from pathlib import Path
import unreal as u

root = Path(__file__).resolve().parents[2]
for octave in range(4, 8):
    source = root / "ArtSource" / "Audio" / f"ToothPianoC{octave}.wav"
    assert source.is_file(), source
    task = u.AssetImportTask()
    task.set_editor_properties(dict(
        filename=str(source), destination_path="/Game/Audio", destination_name=f"S_ToothPianoC{octave}",
        automated=True, replace_existing=True, save=True, factory=u.SoundFactory(),
    ))
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    sound = u.EditorAssetLibrary.load_asset(f"/Game/Audio/S_ToothPianoC{octave}")
    assert isinstance(sound, u.SoundWave)
    sound.set_editor_property("looping", False)
    assert u.EditorAssetLibrary.save_loaded_asset(sound, only_if_is_dirty=False)
    assert abs(sound.get_editor_property("duration") - 3.6 * .77 ** (octave - 4)) < .01
    print(f"MC_TOOTH_PIANO_AUDIO_IMPORTED {sound.get_path_name()}")
