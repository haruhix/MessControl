"""Import the first Tooth Fairy portrait as a new UI texture, preserving existing assets.

Run with the MessControl editor closed, using its headless Python commandlet:
UnrealEditor-Cmd.exe E:/DEVGAME/MessControl/MessControl.uproject
    -run=pythonscript -script=E:/DEVGAME/MessControl/Tools/Unreal/import_fairy_portrait.py
    -unattended -NoSourceControl -NoSplash -NullRHI

Only /Game/Art/UI/T_ToothFairyPortrait_v1 is created and saved. No map is opened or saved.
The script refuses to replace a texture already present at that destination.
"""

from pathlib import Path
import struct

import unreal as u


DESTINATION = "/Game/Art/UI"
ASSET_NAME = "T_ToothFairyPortrait_v1"
ASSET_PATH = f"{DESTINATION}/{ASSET_NAME}"


def main():
    project_root = Path(u.Paths.project_dir()).resolve()
    if not (project_root / "MessControl.uproject").is_file():
        raise RuntimeError("Run this import in the MessControl project, not another open editor project.")
    source = project_root / "ArtSource" / "UI" / "ToothFairy_v1.png"
    if not source.is_file():
        raise RuntimeError(f"Tooth Fairy source image is missing: {source}")
    with source.open("rb") as image_file:
        header = image_file.read(33)
    if len(header) != 33 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise RuntimeError(f"Expected a valid PNG source image: {source}")
    width, height = struct.unpack(">II", header[16:24])
    if not (0 < width <= 16384 and 0 < height <= 16384):
        raise RuntimeError(f"Invalid PNG dimensions: {width} x {height}")

    library = u.EditorAssetLibrary
    saved_package = project_root / "Content" / "Art" / "UI" / f"{ASSET_NAME}.uasset"
    if saved_package.exists() or library.does_asset_exist(ASSET_PATH):
        raise RuntimeError(f"Refusing to replace the existing asset: {ASSET_PATH}")

    # Enum names correspond to UE 5.8 Engine/TextureDefines.h. TC_EditorIcon is
    # the uncompressed RGBA8 mode; this version has no TC_UserInterface2D entry.
    compression = u.TextureCompressionSettings.TC_EDITOR_ICON
    ui_group = u.TextureGroup.TEXTUREGROUP_UI
    no_mips = u.TextureMipGenSettings.TMGS_NO_MIPMAPS
    task = u.AssetImportTask()
    task.set_editor_properties(dict(
        filename=str(source),
        destination_path=DESTINATION,
        destination_name=ASSET_NAME,
        automated=True,
        replace_existing=False,
        replace_existing_settings=False,
        save=False,
        factory=u.TextureFactory(),
    ))
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    imported_paths = list(task.get_editor_property("imported_object_paths"))
    expected_object_path = f"{ASSET_PATH}.{ASSET_NAME}"
    if imported_paths != [expected_object_path]:
        raise RuntimeError(f"Import returned unexpected assets: {imported_paths}")
    texture = library.load_asset(ASSET_PATH)
    if not isinstance(texture, u.Texture2D):
        raise RuntimeError(f"Import did not create a Texture2D at {ASSET_PATH}")

    texture.set_editor_properties(dict(
        compression_settings=compression,
        lod_group=ui_group,
        mip_gen_settings=no_mips,
        srgb=True,
        compression_no_alpha=False,
        adjust_min_alpha=0.0,
        adjust_max_alpha=1.0,
        never_stream=True,
    ))
    for property_name, expected in (
        ("compression_settings", compression),
        ("lod_group", ui_group),
        ("mip_gen_settings", no_mips),
        ("srgb", True),
        ("compression_no_alpha", False),
    ):
        actual = texture.get_editor_property(property_name)
        if actual != expected:
            raise RuntimeError(f"Portrait setting failed: {property_name}={actual}, expected {expected}")
    if not library.save_loaded_asset(texture, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save {ASSET_PATH}")
    print(f"MC_FAIRY_PORTRAIT_IMPORTED asset={texture.get_path_name()} width={width} height={height} alpha_preserved=1")


if __name__ == "__main__":
    main()
