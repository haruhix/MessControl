"""Import original PSD cosmetic glyphs and bind only matching existing cosmetic IDs.
Perk artwork remains exclusively DT_Perks.Icon. No product, price or gameplay rows change.
"""
import json
from pathlib import Path
import unreal as u
assert not u.EditorLevelLibrary.get_pie_worlds(True)
root=Path(u.Paths.project_dir()).resolve();assert root.drive.upper()=='E:'
out=Path('E:/CodexWork/MessControl/Validation/UICommerce_20261104')
bp=u.load_asset('/Game/Gameplay/UI/V2/WBP_UIWardrobe');assert bp
cls=u.load_class(None,'/Game/Gameplay/UI/V2/WBP_UIWardrobe.WBP_UIWardrobe_C');defaults=u.get_default_object(cls)
art={str(k):v for k,v in defaults.get_editor_property('cosmetic_icon_art').items()}
table=u.load_asset('/Game/Data/DT_CharacterCosmetics');assert table
rows=json.loads(u.DataTableFunctionLibrary.export_data_table_to_json_string(table));ids={r['Name'] for r in rows}
source=root/'ArtSource/UI/V2/Icons/Wardrobe'
manifest=json.loads((source/'RuntimeGlyphManifest.json').read_text(encoding='utf-8'))
known={r['name']:r for r in manifest['icons']}
bindings=[('Hat_Head_CowboyHat','CowboyHat'),('Hat_Head_FootballHelmet','FootballHelmet'),('Face_Head_SquareGlasses','SquareGlasses')]
report=[]
for item,name in bindings:
    assert item in ids,(item,sorted(ids))
    glyph=Path(known[name]['glyph']);assert glyph.is_file()
    path='/Game/Gameplay/UI/V2/Art/Wardrobe/T_UI_PSD_'+name
    if not u.EditorAssetLibrary.does_asset_exist(path):
        task=u.AssetImportTask();task.filename=str(glyph);task.destination_path='/Game/Gameplay/UI/V2/Art/Wardrobe'
        task.destination_name='T_UI_PSD_'+name;task.automated=True;task.save=True
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex=u.load_asset(path);assert tex
    tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property('mip_gen_settings',u.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('lod_group',u.TextureGroup.TEXTUREGROUP_UI);tex.set_editor_property('never_stream',True)
    assert u.EditorAssetLibrary.save_loaded_asset(tex,only_if_is_dirty=False)
    art[item]=tex;report.append({'itemID':item,'texture':path,'sourceGlyph':str(glyph)})
defaults.set_editor_property('cosmetic_icon_art',art)
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert u.EditorAssetLibrary.save_loaded_asset(bp,only_if_is_dirty=False)
(out/'WardrobeArt.json').write_text(json.dumps({'bindings':report,'unmapped':'Other actual cosmetics retain their existing catalog art. DentalFloss has no matching current item and is not added.','perkArt':'DT_Perks.Icon unchanged.'},ensure_ascii=False,indent=2),encoding='utf-8')
print('ORIGINAL_WARDROBE_ART_BOUND',len(report))
