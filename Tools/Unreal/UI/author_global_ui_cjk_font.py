"""Create one saved Roboto+CJK runtime composite and point three theme roles at it.

Run only in the project-owned idle editor after the approved Editor build.
Does not modify engine fonts, the compact font, boss fonts, widgets or global culture.
"""
import hashlib
import json
import shutil
from pathlib import Path

import unreal as u

ROOT=Path(u.Paths.project_dir()).resolve()
OUT=Path('E:/CodexWork/MessControlLocalization_20261019/FontAuthoring')
SOURCE='/Engine/EngineFonts/Roboto'
TARGET='/Game/Gameplay/UI/Fonts/F_MCUI_Global'
CJK='/Game/Gameplay/UI/Fonts/FF_MCBossCJK_Regular'
THEME='/Game/Gameplay/UI/Theme/DA_GameplayUITheme'


def require(ok,message):
    if not ok:
        raise RuntimeError(message)


def balanced_field(text,name):
    start=text.index(name+'=')+len(name)+1
    level=0
    for index in range(start,len(text)):
        if text[index]=='(':
            level+=1
        elif text[index]==')':
            level-=1
        if level==0:
            return text[start:index+1]
    raise RuntimeError('Unbalanced exported font field: '+name)


def main():
    require(ROOT.drive.upper()=='E:','Task storage must be on E:')
    require((ROOT/'MessControl.uproject').is_file(),'Expected MessControl project')
    require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'Stop PIE before authoring fonts')
    OUT.mkdir(parents=True,exist_ok=True)
    lib=u.EditorAssetLibrary
    engine_font=u.load_asset(SOURCE)
    face=u.load_asset(CJK)
    theme=u.load_asset(THEME)
    dirty={p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not dirty.intersection((THEME,TARGET)),'Preserve unsaved shared theme/font edits before authoring')
    require(isinstance(engine_font,u.Font) and isinstance(face,u.FontFace),'Expected saved runtime font and CJK FontFace')
    require(engine_font.get_editor_property('font_cache_type')==u.FontCacheType.RUNTIME,'Engine Roboto is not a runtime font')
    # Existing asset already owns embedded font bytes. This source check records
    # provenance; the packaged game never opens an ArtSource or engine TTF path.
    source_file=ROOT/'ArtSource/UI/BossReferenceMatch_20261017/Fonts/DroidSansFallback.ttf'
    require(source_file.is_file(),'CJK provenance source is missing')
    require(hashlib.sha256(source_file.read_bytes()).hexdigest()=='05d71b179ef97b82cf1bb91cef290c600a510f77f39b4964359e3ef88378c79d','Unexpected CJK font source')
    theme_file=ROOT/'Content/Gameplay/UI/Theme/DA_GameplayUITheme.uasset'
    backup=OUT/'DA_GameplayUITheme.before.uasset'
    if not backup.exists():
        shutil.copy2(theme_file,backup)
    before_values={name:theme.get_editor_property(name).copy() for name in ('body_font','heading_font','display_font','compact_heading_font','palette')}
    before={name:value.export_text() for name,value in before_values.items()}
    for name in ('body_font','heading_font','display_font'):
        previous=before_values[name].get_editor_property('font_object')
        require(previous and previous.get_path_name() in (engine_font.get_path_name(),TARGET+'.F_MCUI_Global'),'Preserving an independently customized theme font: '+name)
    font=u.load_asset(TARGET) if lib.does_asset_exist(TARGET) else lib.duplicate_asset(SOURCE,TARGET)
    require(isinstance(font,u.Font),'Could not create shared runtime composite')
    require(font.get_editor_property('font_cache_type')==u.FontCacheType.RUNTIME,'Shared font must use runtime caching')
    original=font.get_editor_property('composite_font').export_text()
    primary=balanced_field(original,'DefaultTypeface')
    require('RobotoRegular' in primary and 'RobotoBold' in primary,'Shared font must retain both original Roboto weights')
    # Make the saved composite authoritative; do not inherit editor-only locale
    # selections from CoreStyleDefault at runtime.
    font.set_editor_property('runtime_font_source',u.RuntimeFontSource.ASSET)
    require(u.MCGameplayUIEditorLibrary.configure_boss_name_cjk_fallback(font,face),'CJK sub-typeface authoring failed')
    after_font=font.get_editor_property('composite_font').export_text()
    require(balanced_field(after_font,'DefaultTypeface')==primary,'Primary Latin/Cyrillic typefaces changed')
    require(face.get_path_name() in after_font,'Shared font has no saved CJK face reference')
    for name in ('body_font','heading_font','display_font'):
        value=theme.get_editor_property(name).copy()
        value.set_editor_property('font_object',font)
        theme.set_editor_property(name,value)
    after={name:theme.get_editor_property(name).export_text() for name in before}
    require(before['compact_heading_font']==after['compact_heading_font'],'Compact font must remain unchanged')
    require(before['palette']==after['palette'],'Palette changed unexpectedly')
    for name in ('body_font','heading_font','display_font'):
        original_value=theme.get_editor_property(name).copy()
        # Replacing only FontObject preserves size, typeface, outline, spacing,
        # material and every other authored font setting.
        original_value.set_editor_property('font_object',before_values[name].get_editor_property('font_object'))
        require(original_value.export_text()==before[name],'Non-object typography setting changed: '+name)
    require(lib.save_loaded_asset(font,only_if_is_dirty=False),'Shared font save failed')
    require(lib.save_loaded_asset(theme,only_if_is_dirty=False),'Theme save failed')
    report={'status':'authored','font':font.get_path_name(),'cjk_face':face.get_path_name(),
        'before_theme':before,'after_theme':after,'font_before':original,'font_after':after_font,
        'unchanged':['engine fonts','compact font','boss fonts','palette','font sizes/typefaces/outlines'],
        'backups':str(backup),'rendered_validation':'pending in HUD, card body/rich text, dropdown, editable hint and main menu',
        'packaged_validation':'not performed by this editor authoring script',
        'limitations':'Droid Sans Fallback covers common BMP CJK and all precomposed Hangul, not all Unicode/rare Han extensions; regional glyph typography is shared.'}
    (OUT/'Report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    u.log('MC_SHARED_RUNTIME_FONT_AUTHORED '+str(OUT/'Report.json'))
    return report


if __name__=='__main__':
    main()
