"""Explicitly replace the studio ident with the user's 2026-10-22 clean wordmark.

Asset-only authoring in the idle, saved MessControl Editor. The exact hold uses
the supplied silhouette; independent component masks animate the seven glyphs.
No C++ build, global Time node, movie decoder, gameplay or shared-theme rewrite.
Run only for this replacement; backups and all preview frames stay on E:.
"""
from pathlib import Path
import hashlib
import json
import unreal as u

ROOT=Path('E:/DEVGAME/MessControl')
SOURCE=ROOT/'ArtSource/Brand/Branch9/CleanWordmark_20261022'
OUT=Path(globals().get('MC_BRANCH9_CLEAN_OUTPUT','E:/CodexWork/Branch9CleanIntro_20261022/Authoring01'))
ART='/Game/Gameplay/UI/Brand/Branch9Clean'
WIDGET='/Game/Gameplay/UI/WBP_StudioSplash'
MATERIAL=ART+'/M_Branch9_Kinetic'
lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
def require(value,message):
 if not value:raise RuntimeError(message)

def dirty():
 return list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())

def save(asset):
 require(lib.save_loaded_asset(asset,only_if_is_dirty=False),'Save failed: '+asset.get_path_name())

def backup(package):
 src=ROOT/'Content'/(package.removeprefix('/Game/')+'.uasset')
 if not src.is_file():return
 dst=OUT/'BeforePackages'/src.relative_to(ROOT/'Content')
 dst.parent.mkdir(parents=True,exist_ok=True)
 data=src.read_bytes();dst.write_bytes(data)
 require(hashlib.sha256(data).digest()==hashlib.sha256(dst.read_bytes()).digest(),'Backup verification failed')
 report['backups'].append({'package':package,'file':str(dst),'sha256':hashlib.sha256(data).hexdigest()})

def texture(name):
 path=ART+'/'+name
 require(not lib.does_asset_exist(path),'Preserve existing owned asset; inspect before rerun: '+path)
 task=u.AssetImportTask()
 task.set_editor_properties(dict(filename=str(SOURCE/(name+'.png')),destination_path=ART,destination_name=name,
  automated=True,replace_existing=False,save=False))
 assets.import_asset_tasks([task])
 tex=u.load_asset(path);require(isinstance(tex,u.Texture2D),'Texture import failed: '+path)
 tex.set_editor_properties(dict(compression_settings=u.TextureCompressionSettings.TC_EDITOR_ICON,
  lod_group=u.TextureGroup.TEXTUREGROUP_UI,mip_gen_settings=u.TextureMipGenSettings.TMGS_NO_MIPMAPS,
  never_stream=True,srgb=True,address_x=u.TextureAddress.TA_CLAMP,address_y=u.TextureAddress.TA_CLAMP))
 lib.set_metadata_tag(tex,'MessControl.Branch9Clean.SourceSHA256',hashlib.sha256((SOURCE/(name+'.png')).read_bytes()).hexdigest())
 save(tex)
 report['textures'].append({'asset':path,'dimensions':[tex.blueprint_get_size_x(),tex.blueprint_get_size_y()]})
 return tex

def make_shader(manifest):
 glyphs=manifest['glyphs']
 require([g['name'] for g in glyphs]==list('BRANCH9'),'Expected seven glyph masks in exact order')
 centers=', '.join('float2(%.9f,%.9f)'%tuple(g['center']) for g in glyphs)
 atlas=', '.join('float2(%.9f,%.9f)'%tuple(g['atlas_center']) for g in glyphs)
 # Inside B's thick left stem, never in a counter. This distinguishes the
 # explicitly transparent legacy layers from the full-logo WordmarkTexture.
 b=glyphs[0]['bbox'];probe=((b[0]+8)/2048,(b[1]+b[3])*.5/768)
 return '''// Source-locked 7-glyph motion. Reveal is native CubicOut, never global Time.
float presence=Texture2DSample(LogoTexture,LogoTextureSampler,float2(PROBE_X,PROBE_Y)).a;
if(presence<0.01) return float4(0,0,0,0);
float clock=1.0-pow(max(1.0-saturate(Reveal),0.0),1.0/3.0);
// Counteract the inherited native fade-out translation for a clean stationary fade.
float2 uv=UV+float2(0.01875*(1.0-saturate(Opacity)),0.0);
if(clock>=0.965 || ReducedMotion>0.5)
{
 float4 hold=Texture2DSample(LogoTexture,LogoTextureSampler,uv);
 float gate=ReducedMotion>0.5?smoothstep(0.0,0.30,clock):1.0;
 return float4(hold.rgb*LogoColor.rgb,hold.a*LogoColor.a*saturate(Opacity)*gate);
}
float2 centers[7]={CENTERS};
float2 atlasCenters[7]={ATLAS};
float2 pixel=uv*float2(2048.0,768.0);
float coverage=0.0;
float3 rgb=0.0;
[unroll] for(int i=0;i<7;i++)
{
 // A and its occluded N form one interlocking motif and enter together.
 float start=i==6?0.44:(i==3?2.0:float(i))*0.052;
 float duration=i==6?0.50:0.30;
 float p=saturate((clock-start)/duration);
 if(p<=0.0) continue;
 float remaining=pow(1.0-p,4.0);
 float2 move=(i==6?float2(44.0,25.0):float2(-24.0,12.0))*remaining*Travel;
 float angle=(i==6?NineAngle:0.0)*0.01745329252*remaining;
 float c=cos(angle),s=sin(angle);
 float2 relative=pixel-centers[i]-move;
 float2 q=float2(c*relative.x+s*relative.y,-s*relative.x+c*relative.y);
 // Independent component atlas tiles preserve overlapping R/A and A/N bounds.
 if(abs(q.x)>254.0 || abs(q.y)>254.0) continue;
 float2 atlasUV=(q+atlasCenters[i])/float2(2048.0,1024.0);
 float4 source=Texture2DSample(GlyphAtlas,GlyphAtlasSampler,atlasUV);
 float gate=smoothstep(0.0,0.30,p);
 float a=source.a*gate;
 rgb+=source.rgb*a;coverage+=a;
 // One short, faint directional trace. It is absent before the reading hold.
 float echoGate=sin(saturate(p/0.60)*3.14159265359)*(1.0-step(0.60,p));
 float2 echoUV=(q+float2(10.0,-3.0)*(1.0-p)+atlasCenters[i])/float2(2048.0,1024.0);
 float4 echo=Texture2DSample(GlyphAtlas,GlyphAtlasSampler,echoUV);
 float ea=echo.a*gate*echoGate*EchoStrength;
 rgb+=echo.rgb*ea;coverage+=ea;
}
return float4(rgb/max(coverage,0.0001)*LogoColor.rgb,saturate(coverage)*LogoColor.a*saturate(Opacity));
'''.replace('PROBE_X','%.9f'%probe[0]).replace('PROBE_Y','%.9f'%probe[1]).replace('CENTERS',centers).replace('ATLAS',atlas)

require(OUT.drive.upper()=='E:' and SOURCE.drive.upper()=='E:','All output must be on E:')
require(not OUT.exists() or not any(OUT.iterdir()),'Choose fresh authoring output')
require(not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(),'Preserve active Play')
require(not dirty(),'Preserve unsaved editor changes')
OUT.mkdir(parents=True,exist_ok=True)
report={'succeeded':False,'backups':[],'textures':[],'previews':[]}
try:
 backup(WIDGET)
 manifest=json.loads((SOURCE/'OutputManifest.json').read_text(encoding='utf8'))
 require(manifest['canvas']==[2048,768] and manifest['atlas_size']==[2048,1024],'Unexpected source contract')
 full=texture('T_Branch9_CleanWordmark')
 atlas=texture('T_Branch9_CleanGlyphAtlas')
 blank=texture('T_Branch9_Transparent')
 require(not lib.does_asset_exist(MATERIAL),'Preserve existing kinetic material before rerun')
 mat=assets.create_asset('M_Branch9_Kinetic',ART,u.Material,u.MaterialFactoryNew())
 mat.set_editor_properties(dict(material_domain=u.MaterialDomain.MD_UI,blend_mode=u.BlendMode.BLEND_TRANSLUCENT))
 def node(cls,x,y,**props):
  n=edit.create_material_expression(mat,cls,x,y);require(n,'Material node failed')
  n.set_editor_properties(props);return n
 def scalar(name,value,y):
  return node(u.MaterialExpressionScalarParameter,-1000,y,parameter_name=name,default_value=value,group='Studio Motion')
 uv=node(u.MaterialExpressionTextureCoordinate,-1000,-800)
 logo=node(u.MaterialExpressionTextureObjectParameter,-1000,-600,parameter_name='LogoTexture',texture=full,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
 glyph=node(u.MaterialExpressionTextureObjectParameter,-1000,-400,parameter_name='GlyphAtlas',texture=atlas,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
 color=node(u.MaterialExpressionVectorParameter,-1000,200,parameter_name='LogoColor',default_value=u.LinearColor(1,1,1,1))
 inputs={'UV':(uv,''),'LogoTexture':(logo,''),'GlyphAtlas':(glyph,''),'Reveal':(scalar('Reveal',1.,-200),''),
  'Opacity':(scalar('Opacity',1.,0),''),'LogoColor':(color,'RGBA'),'Travel':(scalar('Travel',1.,400),''),
  'EchoStrength':(scalar('EchoStrength',.055,600),''),'NineAngle':(scalar('NineAngle',6.,800),''),
  'ReducedMotion':(scalar('ReducedMotion',0.,1000),'')}
 code=make_shader(manifest)
 shader=node(u.MaterialExpressionCustom,-100,0,description='BRANCH9 exact glyph assembly; native Reveal clock',code=code,output_type=u.CustomMaterialOutputType.CMOT_FLOAT4)
 custom_inputs=[]
 for name in inputs:
  ci=u.CustomInput();ci.set_editor_property('input_name',name);custom_inputs.append(ci)
 shader.set_editor_property('inputs',custom_inputs)
 for name,(n,pin) in inputs.items():require(edit.connect_material_expressions(n,pin,shader,name),'Material input failed: '+name)
 rgb=node(u.MaterialExpressionComponentMask,350,-100,r=True,g=True,b=True,a=False)
 alpha=node(u.MaterialExpressionComponentMask,350,150,r=False,g=False,b=False,a=True)
 for output in [rgb,alpha]:require(edit.connect_material_expressions(shader,'',output,''),'Output mask failed')
 require(edit.connect_material_property(rgb,'',u.MaterialProperty.MP_EMISSIVE_COLOR),'UI color output failed')
 require(edit.connect_material_property(alpha,'',u.MaterialProperty.MP_OPACITY),'UI alpha output failed')
 errors=[str(x) for x in edit.recompile_material(mat)]
 report['material_compile_errors']=errors
 require(not errors,'Material compile errors: '+str(errors))
 save(mat)
 (OUT/'BRANCH9_Kinetic.hlsl').write_text(code,encoding='utf8')
 bp=u.load_asset(WIDGET);require(bp,'Existing splash WBP missing')
 cls=u.load_class(None,WIDGET+'.WBP_StudioSplash_C');cdo=u.get_default_object(cls)
 require(isinstance(cdo,u.MCStudioSplashWidget),'Preserve unrelated splash parent')
 bp.modify();cdo.modify()
 props=dict(wordmark_texture=full,branch_texture=blank,leaves_texture=blank,reveal_material=mat,
  duration=3.,use_native_timeline=True,word_reveal_start=.12,word_reveal_end=1.12,
  fade_out_seconds=.33,background_color=u.LinearColor(0,0,0,1),logo_color=u.LinearColor(1,1,1,1))
 cdo.set_editor_properties(props)
 u.BlueprintEditorLibrary.compile_blueprint(bp)
 require('ERROR' not in str(bp.get_editor_property('status')).upper(),'WBP compilation failed')
 save(bp)
 cls=u.load_class(None,WIDGET+'.WBP_StudioSplash_C');cdo=u.get_default_object(cls)
 require(cdo.get_editor_property('wordmark_texture')==full and cdo.get_editor_property('reveal_material')==mat,'Saved CDO lost hard references')
 report['widget']={'asset':WIDGET,'status':str(bp.get_editor_property('status')),'duration':float(cdo.get_editor_property('duration')),'new_art':full.get_path_name(),'new_material':mat.get_path_name(),'legacy_layers_are_transparent':all(cdo.get_editor_property(k)==blank for k in ['branch_texture','leaves_texture'])}
 require(not dirty(),'Authoring left unexpected dirty packages')
 for index,t in enumerate([0.,.16,.26,.40,.58,.78,.96,1.12,1.60,2.60,2.80,3.]):
  path=OUT/'Keyframes'/('Frame_%02d_%.2f.png'%(index,t))
  require(u.MCGameplayUIEditorLibrary.render_gameplay_ui_preview(cls,str(path),u.IntPoint(1920,1080),'Default','en',t),'Actual UMG preview failed')
  report['previews'].append({'time':t,'path':str(path)})
 report['succeeded']=True
finally:
 report['dirty_after']=[p.get_path_name() for p in dirty()]
 (OUT/'Authoring.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('BRANCH9_CLEAN_AUTHOR_COMPLETE '+str(OUT/'Authoring.json'))
