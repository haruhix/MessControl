"""Fit seven product renders to existing thumbnail frames without changing the widget layout."""
import unreal as u,json,shutil
from pathlib import Path
ROOT=Path(u.Paths.project_dir()).resolve();OUT=ROOT/'Saved/HeadCosmetics20261024';PATH='/Game/Gameplay/UI/WBP_WardrobeCosmeticTile';bp=u.load_asset(PATH)
assert PATH not in {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
backup=OUT/'BeforePackages/Content/Gameplay/UI/WBP_WardrobeCosmeticTile.uasset';backup.parent.mkdir(parents=True,exist_ok=True)
if not backup.exists():shutil.copy2(ROOT/'Content/Gameplay/UI/WBP_WardrobeCosmeticTile.uasset',backup)
cdo=u.get_default_object(u.EditorAssetLibrary.load_blueprint_class(PATH));before=dict(cdo.get_editor_property('ThumbnailUVRegions'));regions=dict(before)
for name,v in json.loads((ROOT/'ArtSource/CharacterCurrent/HeadCosmetics20261024/Icons/IconRegions.json').read_text()).items():regions[u.Name(name)]=u.Vector4(*v)
cdo.modify();cdo.set_editor_property('ThumbnailUVRegions',regions);u.BlueprintEditorLibrary.compile_blueprint(bp);assert u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
after=dict(u.get_default_object(u.EditorAssetLibrary.load_blueprint_class(PATH)).get_editor_property('ThumbnailUVRegions'));assert len(after)==len(before)+7;assert all(after[k]==v for k,v in before.items());print('HEAD_COSMETIC_THUMBNAILS_FITTED',len(after))
