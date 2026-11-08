"""Author owned Food.AutoMouth mesh/materials in an idle existing Editor.

Run through Tools/Unreal/remote_python.py. Never saves the level or donor assets.
The mesh is an original 12-triangle uneven crumb, measured in centimetres.
"""
import json
from pathlib import Path
import unreal as u

OUT=Path('E:/CodexWork/MessControl/Validation/FoodAutoMouth_20261108')
ROOT='/Game/Gameplay/Food/VFX'
SOURCE=Path('E:/DEVGAME/MessControl/ArtSource/FoodAutoMouth')
lib=u.EditorAssetLibrary
tools=u.AssetToolsHelpers.get_asset_tools()
edit=u.MaterialEditingLibrary
assert not u.EditorLevelLibrary.get_pie_worlds(True),'Preserve active PIE'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved map'
allowed={ROOT+'/'+n for n in ('M_FoodCrumb','M_FoodMote','SM_FoodCrumb','NS_Food_AutoMouth','DA_Food_AutoMouth','NET_Food_AutoMouth')}
assert {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}.issubset(allowed),'Preserve unrelated edits'
OUT.mkdir(parents=True,exist_ok=True)
SOURCE.mkdir(parents=True,exist_ok=True)

def node(mat,kind,x,y,**props):
 n=edit.create_material_expression(mat,kind,x,y)
 if props:n.set_editor_properties(props)
 return n

def connect(a,p,b,q):
 assert edit.connect_material_expressions(a,p,b,q),(a,p,b,q)

def output(a,p,prop):
 assert edit.connect_material_property(a,p,prop),(a,p,prop)

def save(a):
 assert lib.save_loaded_asset(a,only_if_is_dirty=False),a.get_path_name()

created=[]
# CullReaction belongs to the Effect Type in UE5.8 and cannot be overridden per
# system. One-shots need Kill, not the donor family's Asleep/reactivate policy.
effect_path=ROOT+'/NET_Food_AutoMouth'
if not lib.does_asset_exist(effect_path):
 effect=lib.duplicate_asset('/Game/Gameplay/ActiveSkills/VFX/NET_ActiveSkills',effect_path);assert effect
 effect.set_editor_property('cull_reaction',u.NiagaraCullReaction.DEACTIVATE)
 save(effect);created.append(effect_path)
 (OUT/'NET_Food_AutoMouth.created.json').write_text(json.dumps(dict(path=effect_path,source='/Game/Gameplay/ActiveSkills/VFX/NET_ActiveSkills',cull_reaction='Deactivate')),encoding='utf8')
else:
 effect=u.load_asset(effect_path)
 assert effect.get_editor_property('cull_reaction')==u.NiagaraCullReaction.DEACTIVATE,'Preserve unexpected EffectType policy; review before applying'

for name,sprite in [('M_FoodCrumb',False),('M_FoodMote',True)]:
 path=ROOT+'/'+name
 if lib.does_asset_exist(path):
  assert (OUT/(name+'.created.json')).exists(),'Do not overwrite existing artwork'
  continue
 mat=tools.create_asset(name,ROOT,u.Material,u.MaterialFactoryNew());assert mat
 (OUT/(name+'.created.json')).write_text(json.dumps(dict(path=path)),encoding='utf8')
 mat.set_editor_properties(dict(two_sided=True,blend_mode=u.BlendMode.BLEND_TRANSLUCENT if sprite else u.BlendMode.BLEND_OPAQUE,
   shading_model=u.MaterialShadingModel.MSM_UNLIT if sprite else u.MaterialShadingModel.MSM_DEFAULT_LIT))
 edit.set_material_usage(mat,u.MaterialUsage.MATUSAGE_NIAGARA_SPRITES if sprite else u.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)
 color=node(mat,u.MaterialExpressionParticleColor,-700,0)
 gain=node(mat,u.MaterialExpressionMultiply,-420,-80,const_b=.65 if sprite else .12)
 connect(color,'RGB',gain,'A');output(gain,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
 if sprite:
  uv=node(mat,u.MaterialExpressionTextureCoordinate,-700,220)
  field=node(mat,u.MaterialExpressionCustom,-420,200,description='Original texture-free soft food dust; no additive glow',
    code='float2 p=UV*2-1; return pow(saturate(1-dot(p,p)),2)*A*.38;',output_type=u.CustomMaterialOutputType.CMOT_FLOAT1)
  inputs=[]
  for label in ('UV','A'):
   entry=u.CustomInput();entry.set_editor_property('input_name',label);inputs.append(entry)
  field.set_editor_property('inputs',inputs)
  connect(uv,'',field,'UV');connect(color,'A',field,'A');output(field,'',u.MaterialProperty.MP_OPACITY)
 else:
  output(color,'RGB',u.MaterialProperty.MP_BASE_COLOR)
  rough=node(mat,u.MaterialExpressionConstant,-200,180,r=.78);output(rough,'',u.MaterialProperty.MP_ROUGHNESS)
  spec=node(mat,u.MaterialExpressionConstant,-200,300,r=.18);output(spec,'',u.MaterialProperty.MP_SPECULAR)
 edit.recompile_material(mat);save(mat);created.append(path)

meshpath=ROOT+'/SM_FoodCrumb'
if not lib.does_asset_exist(meshpath):
 # Hand-authored uneven closed convex box. The 12 triangles stay cheap for many cuts.
 vertices=[(-6,-4,-3),(5,-5,-2),(6,4,-3),(-5,5,-2),(-4,-3,4),(4,-4,3),(5,3,5),(-6,4,3)]
 faces=[(1,3,2),(1,4,3),(5,6,7),(5,7,8),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,4,8),(3,8,7),(4,1,5),(4,5,8)]
 lines=['# Original MessControl food crumb; centimetres, 12 triangles']
 lines+=['v %s %s %s'%v for v in vertices];lines+=['f %s %s %s'%f for f in faces]
 source=SOURCE/'SM_FoodCrumb.obj';source.write_text('\n'.join(lines)+'\n',encoding='utf8')
 task=u.AssetImportTask();task.set_editor_properties(dict(filename=str(source),destination_path=ROOT,destination_name='SM_FoodCrumb',automated=True,replace_existing=False,save=False))
 opt=u.FbxImportUI();opt.set_editor_properties(dict(import_mesh=True,import_materials=False,import_textures=False,import_as_skeletal=False,mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH))
 opt.static_mesh_import_data.set_editor_properties(dict(auto_generate_collision=False,generate_lightmap_u_vs=False,
    normal_import_method=u.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS,convert_scene=False,convert_scene_unit=False))
 task.set_editor_property('options',opt);tools.import_asset_tasks([task])
 mesh=u.load_asset(meshpath);assert isinstance(mesh,u.StaticMesh),task.imported_object_paths
 mesh.set_material(0,u.load_asset(ROOT+'/M_FoodCrumb'));save(mesh);created.append(meshpath)
 (OUT/'SM_FoodCrumb.created.json').write_text(json.dumps(dict(path=meshpath,source=str(source))),encoding='utf8')

report=dict(created=created,mesh_triangles=12,ownership='Only task-owned new assets; level/donors untouched',
 readiness='Niagara renderer references retain mesh/materials; Food.AutoMouth profile must be bound to scoped mouth manifest',
 material='Opaque DefaultLit crumbs, roughness .78, bounded .12 fill; 10 soft non-additive motes, texture-free')
(OUT/'Resources.json').write_text(json.dumps(report,indent=2),encoding='utf8')
u.log('FOOD_AUTOMOUTH_RESOURCES '+json.dumps(report))
