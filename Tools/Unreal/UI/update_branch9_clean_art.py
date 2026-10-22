"""Reimport the explicitly requested studio-art revision without rebuilding C++.

Preserves WBP layout/timing and material motion. Updates the two source textures
and the atlas coordinates embedded in the owned kinetic material. Idle saved
editor required; no Play/session termination is performed by this tool.
"""
import ast
import hashlib
import json
from pathlib import Path
import unreal as u

ROOT=Path('E:/DEVGAME/MessControl')
SOURCE=ROOT/'ArtSource/Brand/Branch9/CleanWordmark_20261022'
OUT=Path(globals().get('MC_BRANCH9_UPDATE_OUTPUT','E:/CodexWork/Branch9CleanIntro_20261022/NRefine/Authoring02'))
ART='/Game/Gameplay/UI/Brand/Branch9Clean'
lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary

def require(ok,message):
 if not ok:raise RuntimeError(message)

require(OUT.drive.upper()=='E:' and not OUT.exists(),'Choose a fresh E: output')
require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'Preserve active Play; stop it before art authoring')
require(not u.EditorLoadingAndSavingUtils.get_dirty_content_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved editor state')
OUT.mkdir(parents=True)
names=['T_Branch9_CleanWordmark','T_Branch9_CleanGlyphAtlas','M_Branch9_Kinetic']
for name in names:
 src=ROOT/'Content/Gameplay/UI/Brand/Branch9Clean'/(name+'.uasset')
 dst=OUT/'BeforePackages'/src.name;dst.parent.mkdir(parents=True,exist_ok=True)
 data=src.read_bytes();dst.write_bytes(data)
 require(hashlib.sha256(data).digest()==hashlib.sha256(dst.read_bytes()).digest(),'Backup mismatch')
report={'textures':[],'succeeded':False}
try:
 for name in names[:2]:
  task=u.AssetImportTask()
  task.set_editor_properties(dict(filename=str(SOURCE/(name+'.png')),destination_path=ART,destination_name=name,
   automated=True,replace_existing=True,replace_existing_settings=False,save=False))
  u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
  texture=u.load_asset(ART+'/'+name);require(isinstance(texture,u.Texture2D),'Texture import failed')
  texture.set_editor_properties(dict(compression_settings=u.TextureCompressionSettings.TC_EDITOR_ICON,lod_group=u.TextureGroup.TEXTUREGROUP_UI,
   mip_gen_settings=u.TextureMipGenSettings.TMGS_NO_MIPMAPS,never_stream=True,srgb=True,address_x=u.TextureAddress.TA_CLAMP,address_y=u.TextureAddress.TA_CLAMP))
  lib.set_metadata_tag(texture,'MessControl.Branch9Clean.SourceSHA256',hashlib.sha256((SOURCE/(name+'.png')).read_bytes()).hexdigest())
  require(lib.save_loaded_asset(texture,only_if_is_dirty=False),'Texture save failed')
  report['textures'].append(texture.get_path_name())
 # Reuse only the pure shader generator; do not execute the initial authoring
 # script's guarded one-time asset creation or touch its old output folders.
 author=ROOT/'Tools/Unreal/UI/author_branch9_clean_intro.py'
 definition=next(n for n in ast.parse(author.read_text(encoding='utf8')).body if isinstance(n,ast.FunctionDef) and n.name=='make_shader')
 namespace={'require':require}
 exec(compile(ast.Module(body=[definition],type_ignores=[]),str(author),'exec'),namespace)
 manifest=json.loads((SOURCE/'OutputManifest.json').read_text(encoding='utf8'))
 code=namespace['make_shader'](manifest)
 material=u.load_asset(ART+'/M_Branch9_Kinetic')
 shaders=[n for n in edit.get_material_expressions(material) if isinstance(n,u.MaterialExpressionCustom) and str(n.get_editor_property('description')).startswith('BRANCH9 exact glyph assembly')]
 require(len(shaders)==1,'Preserve changed/unrecognized material')
 material.modify();shaders[0].set_editor_property('code',code)
 errors=[str(x) for x in edit.recompile_material(material)];report['material_errors']=errors
 require(not errors,'Material compilation failed')
 require(lib.save_loaded_asset(material,only_if_is_dirty=False),'Material save failed')
 (OUT/'Motion.hlsl').write_text(code,encoding='utf8')
 cls=u.load_class(None,'/Game/Gameplay/UI/WBP_StudioSplash.WBP_StudioSplash_C')
 for index,t in enumerate([.40,.58,.78,1.12,1.6,2.6,2.8]):
  path=OUT/'Keyframes'/('Frame_%02d_%.2f.png'%(index,t))
  require(u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,str(path),u.IntPoint(1920,1080),'Default','en',t),'Actual UMG preview failed')
 require(not u.EditorLoadingAndSavingUtils.get_dirty_content_packages() and not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Unexpected dirty assets')
 report['succeeded']=True
finally:(OUT/'Authoring.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('BRANCH9_ART_UPDATED '+str(OUT))
