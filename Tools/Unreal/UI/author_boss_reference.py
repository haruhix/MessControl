"""Create fully procedural adaptive boss chrome with separate portrait art."""
from pathlib import Path
import json
import runpy
import shutil
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
BASE = '/Game/Gameplay/UI'
SOURCE = ROOT / 'ArtSource/UI/BossReferenceMatch_20261017'
BACKUP = ROOT / 'Saved/GameplayUI/BeforeBossReference'
BACKUP.mkdir(parents=True, exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
lib = u.EditorAssetLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
for relative in ('Theme/DA_GameplayUITheme', 'WBP_BossHealth_v10'):
    original = ROOT / ('Content/Gameplay/UI/' + relative + '.uasset')
    copy = BACKUP / (Path(relative).name + '.uasset')
    if not copy.exists():
        shutil.copy2(original, copy)

runpy.run_path(str(ROOT / 'Tools/Unreal/UI/import_boss_reference_fonts.py'), run_name='__main__')
runpy.run_path(str(ROOT / 'Tools/Unreal/UI/author_procedural_boss_materials.py'), run_name='__main__')
textures = {}
for name in ('T_UI_BossIcon_Caries_Ref', 'T_UI_BossIcon_NutKnight_Ref', 'T_UI_BossIcon_NutMage_Ref'):
    path = BASE + '/Art/' + name
    # These three task-owned icon-only derivatives can be reimported after matte QA.
    task = u.AssetImportTask()
    task.set_editor_property('filename', str(SOURCE / (name + '.png')))
    task.set_editor_property('destination_path', BASE + '/Art')
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', False)
    assets.import_asset_tasks([task])
    texture = u.load_asset(path)
    assert texture, path
    texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('mip_gen_settings', u.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property('lod_group', u.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('srgb', True)
    assert lib.save_loaded_asset(texture, only_if_is_dirty=False)
    textures[name] = texture

path = BASE + '/Materials/M_UI_BossReferenceGrid'
if not lib.does_asset_exist(path):
    material = assets.create_asset('M_UI_BossReferenceGrid', BASE + '/Materials', u.Material, u.MaterialFactoryNew())
    material.set_editor_property('material_domain', u.MaterialDomain.MD_UI)
    material.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    edit = u.MaterialEditingLibrary
    uv = edit.create_material_expression(material, u.MaterialExpressionTextureCoordinate, -600, 0)
    nodes = {'UV': uv}
    for index, (name, value) in enumerate((('ThemeOpacity', 1.), ('Columns', 58.), ('Rows', 5.), ('Segments', 0.), ('Empty', 0.))):
        node = edit.create_material_expression(material, u.MaterialExpressionScalarParameter, -600, 150 + index*90)
        node.set_editor_property('parameter_name', name)
        node.set_editor_property('default_value', value)
        nodes[name] = node
    tint = edit.create_material_expression(material, u.MaterialExpressionVectorParameter, -600, 720)
    tint.set_editor_property('parameter_name', 'ThemeColor')
    tint.set_editor_property('default_value', u.LinearColor(1., .02, .025, 1.))
    nodes['ThemeColor'] = tint
    custom = edit.create_material_expression(material, u.MaterialExpressionCustom, -300, 0)
    inputs = []
    for name in nodes:
        item = u.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    custom.set_editor_property('output_type', u.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('description', 'Reference boss LEDs: five rows, hot core, fixed dot pitch; shield is rectangular segments')
    custom.set_editor_property('code', '''
float2 p=abs(frac(UV*float2(Columns,Rows))-0.5);
if(Segments>0.5) {
    float outside=saturate((0.46-max(p.x,p.y))*35.0);
    float inside=saturate((0.32-max(p.x,p.y))*35.0);
    float a=lerp(outside, saturate(outside-inside), Empty);
    return float4(ThemeColor.rgb,ThemeOpacity*a);
}
float r=length(p);
float disk=saturate((0.31-r)*18.0);
float halo=exp(-r*7.0)*0.30*(1.0-Empty);
float core=saturate((0.19-r)*9.0)*(1.0-Empty);
return float4(lerp(ThemeColor.rgb,ThemeColor.rgb+float3(0.95,0.95,0.95),core),ThemeOpacity*max(disk,halo));
''')
    for name, node in nodes.items():
        assert edit.connect_material_expressions(node, '', custom, name)
    for channels, output, y in (((True, True, True, False), u.MaterialProperty.MP_EMISSIVE_COLOR, 0),
                                ((False, False, False, True), u.MaterialProperty.MP_OPACITY, 180)):
        mask = edit.create_material_expression(material, u.MaterialExpressionComponentMask, 0, y)
        for name, enabled in zip(('r','g','b','a'), channels):
            mask.set_editor_property(name, enabled)
        assert edit.connect_material_expressions(custom, '', mask, '')
        assert edit.connect_material_property(mask, '', output)
    edit.recompile_material(material)
    assert lib.save_loaded_asset(material, only_if_is_dirty=False)
material = u.load_asset(path)

bp_path = BASE + '/WBP_BossHealth_Reference'
bp = u.MCGameplayUIEditorLibrary.create_editable_gameplay_widget(bp_path, u.MCBossHealthWidget)
assert bp
cls = u.load_class(None, bp_path + '.WBP_BossHealth_Reference_C')
defaults = u.get_default_object(cls)
defaults.set_editor_property('led_fill_material', material)
defaults.set_editor_property('frame_material', u.load_asset(BASE + '/Materials/M_UI_BossFrameProcedural'))
defaults.set_editor_property('shield_icon_material', u.load_asset(BASE + '/Materials/M_UI_BossShieldProcedural'))
defaults.set_editor_property('portrait_box_size', u.Vector2D(73.45,73.45))
defaults.set_editor_property('portrait_scale_by_boss', {})
defaults.set_editor_property('preview_scenario', u.MCGameplayUIPreviewScenario.THREE_BOSSES)
for field, name, size in (('boss_name_font','Names',24), ('boss_value_font','Digits',30)):
    font = defaults.get_editor_property(field)
    font.set_editor_property('font_object', u.load_asset(BASE + '/Fonts/FF_MCBoss' + name + '_Regular_Font'))
    font.set_editor_property('typeface_font_name', 'Default')
    font.set_editor_property('size', size)
    defaults.set_editor_property(field, font)
u.BlueprintEditorLibrary.compile_blueprint(bp)
assert lib.save_loaded_asset(bp, only_if_is_dirty=False)

theme = u.load_asset(BASE + '/Theme/DA_GameplayUITheme')
portraits = {u.Name(str(key)): value for key, value in theme.get_editor_property('boss_portraits').items()}
for identity, icon in (('RottenGuardian', 'Caries'), ('NutKnight', 'NutKnight'), ('NutWizard','NutMage')):
    portraits[u.Name(identity)] = textures['T_UI_BossIcon_' + icon + '_Ref']
theme.set_editor_property('boss_portraits', portraits)
mapping = {u.Name(str(key)): value for key, value in theme.get_editor_property('widget_classes').items()}
mapping[u.Name('BossHealth')] = cls
theme.set_editor_property('widget_classes', mapping)
palette = theme.get_editor_property('palette')
def srgb(r,g,b):
    def linear(v):
        v = v / 255.
        return v / 12.92 if v <= .04045 else ((v+.055)/1.055)**2.4
    return u.LinearColor(linear(r),linear(g),linear(b),1.)
palette.set_editor_property('boss_health', srgb(255,43,45))
palette.set_editor_property('shield', srgb(0,239,247))
palette.set_editor_property('boss_surface', srgb(11,12,16))
palette.set_editor_property('boss_rim', srgb(65,71,76))
palette.set_editor_property('boss_rim_highlight', srgb(140,150,156))
theme.set_editor_property('palette', palette)
theme.notify_theme_changed()
assert lib.save_loaded_asset(theme, only_if_is_dirty=False)
u.MCGameplayUIEditorLibrary.ensure_gameplay_ui_preview_registered()
report = {'widget': bp_path, 'textures': list(textures), 'material': path, 'fonts': ['Cuprum derivative names','Tektur derivative digits'], 'backup': str(BACKUP)}
(ROOT / 'Saved/GameplayUI/boss_reference_authoring.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_BOSS_REFERENCE_AUTHOR_COMPLETE ' + json.dumps(report))
