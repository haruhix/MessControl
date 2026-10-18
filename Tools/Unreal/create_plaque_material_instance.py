"""Expose artist controls and create the saved plaque instance used by runtime MIDs."""
import unreal as u,json,re,shutil
from pathlib import Path

root=Path(u.Paths.project_dir());out=root/'Artifacts/GrimeInstance_20261017';out.mkdir(exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
master_path='/Game/Gameplay/Care/M_ArenaToothRelief';instance_path='/Game/Gameplay/Care/MI_ArenaToothGrime'
e=u.MaterialEditingLibrary;lib=u.EditorAssetLibrary;m=lib.load_asset(master_path);assert m
nodes=list(e.get_material_expressions(m))
cs={str(n.get_editor_property('description')):n for n in nodes if isinstance(n,u.MaterialExpressionCustom)}
ps={str(n.get_editor_property('parameter_name')):n for n in nodes if isinstance(n,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter,u.MaterialExpressionTextureObjectParameter))}
if not (out/'M_ArenaToothRelief_Before.uasset').exists():shutil.copy2(root/'Content/Gameplay/Care/M_ArenaToothRelief.uasset',out/'M_ArenaToothRelief_Before.uasset')

def scalar(name,value,group,order,description,limits=None):
 n=ps.get(name)
 if n is None:
  n=e.create_material_expression(m,u.MaterialExpressionScalarParameter,500,5600+len(ps)*90)
  n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',value);ps[name]=n
 n.set_editor_property('group',group);n.set_editor_property('sort_priority',order);n.set_editor_property('desc',description)
 if limits:
  n.set_editor_property('slider_min',limits[0]);n.set_editor_property('slider_max',limits[1])
 return n

def connect_inputs(node,inputs):
 entries=list(node.get_editor_property('inputs'));names={str(i.get_editor_property('input_name')) for i in entries}
 for name in inputs:
  if name not in names:
   item=u.CustomInput();item.set_editor_property('input_name',name);entries.append(item)
 node.set_editor_property('inputs',entries)
 for name,src in inputs.items():assert e.connect_material_expressions(src,'',node,name),name

def replace_once(node,old,new):
 code=node.get_editor_property('code')
 if new in code:return
 assert code.count(old)==1,(node.get_editor_property('description'),old)
 node.set_editor_property('code',code.replace(old,new))

with u.ScopedEditorTransaction('Create editable arena tooth plaque material instance'):
 size=scalar('GrimePigmentSize',420.,'01 Pattern',0,'Размер пятен в сантиметрах. Больше значение — крупнее рисунок.',(40.,1000.))
 pattern=scalar('GrimePatternSeed',0.,'01 Pattern',1,'Меняет случайный рисунок. Каждый зуб дополнительно получает свой вариант.',(0.,100.))
 warp=scalar('GrimeWarpStrength',.10,'01 Pattern',2,'Сила деформации пятен.',(0.,.30))
 opacity=scalar('GrimeOpacity',1.,'02 Coating',0,'Общая прозрачность слоя налёта.',(0.,1.))
 soft=scalar('GrimeEdgeSoftness',.265,'02 Coating',1,'Ширина плавного перехода налёта в эмаль.',(.01,.60))
 erosion=scalar('GrimeEdgeErosion',1.,'02 Coating',2,'Выраженность неровных краёв и размытых разводов.',(0.,2.))
 wash=scalar('GrimeWashOpacity',.62,'02 Coating',3,'Плотность светлых разводов между тёмными пятнами.',(0.,1.))
 rough=scalar('GrimeRoughness',.71,'03 Surface',1,'Шероховатость налёта: от глянцевого к матовому.',(0.,1.))
 tint=ps.get('GrimeTint')
 if tint is None:
  tint=e.create_material_expression(m,u.MaterialExpressionVectorParameter,500,5500)
  tint.set_editor_property('parameter_name','GrimeTint');tint.set_editor_property('default_value',u.LinearColor(1,1,1,1));ps['GrimeTint']=tint
 tint.set_editor_property('group','03 Surface');tint.set_editor_property('sort_priority',0);tint.set_editor_property('desc','Цветовой множитель налёта. Белый сохраняет текущий цвет.')
 for label in ['MC Care: anchored plaque detail pigment','MC Care: coatingClip']:
  node=cs[label]
  if 'PatternSeed' not in node.get_editor_property('code'):
   node.set_editor_property('code',re.sub(r'\bSeed\b','(Seed+PatternSeed)',node.get_editor_property('code')))
  connect_inputs(node,{'PatternSeed':pattern})
 connect_inputs(cs['MC Care: anchored plaque detail pigment'],{'WarpStrength':warp})
 replace_once(cs['MC Care: anchored plaque detail pigment'],'q+=(warp-.5)*.10;','q+=(warp-.5)*max(WarpStrength,0);')
 clip=cs['MC Care: coatingClip']
 connect_inputs(clip,{'Opacity':opacity,'EdgeSoftness':soft,'EdgeErosion':erosion,'WashOpacity':wash})
 replace_once(clip,'boundary+ragged);','boundary+ragged*max(EdgeErosion,0));')
 replace_once(clip,'smoothstep(.025,.29,','smoothstep(.025,.025+max(EdgeSoftness,.001),')
 replace_once(clip,'lerp(.62,1,','lerp(saturate(WashOpacity),1,')
 replace_once(clip,'saturate(feather*paper*wipe);','saturate(feather*paper*wipe)*saturate(Opacity);')
 color=cs['MC Care: coatingColor'];connect_inputs(color,{'ArtistTint':tint})
 replace_once(color,'return lerp(color,float3(.92,.97,.94),F.y*.92);','return lerp(color*max(ArtistTint,0),float3(.92,.97,.94),F.y*.92);')
 surface=cs['MC Care: coatingRoughness'];connect_inputs(surface,{'Roughness':rough})
 replace_once(surface,'lerp(.64,.78,D.z)','saturate(Roughness+(D.z-.5)*.14)')
 for name in ['GrimeMicroRelief','GrimeNormalStrength','GrimeDetailSize','GrimeNormal','GrimeHeight','GrimeSurface']:
  if name in ps:ps[name].set_editor_property('group','04 Micro Surface')
 ps['GrimePigment'].set_editor_property('group','01 Pattern')
 for name in ['GrimeAmount','GrimeSeed','BrushAge','GrimeWipe','GrimeMin','GrimeSize','GrimeScale','BrushLocal','GrimeColor']:
  if name in ps:ps[name].set_editor_property('group','90 Runtime Automatic')
 for name in ['GrimeLightColor','GrimeDarkColor','GrimeRimColor']:
  if name in ps:ps[name].set_editor_property('group','99 Legacy')
 errors=list(e.recompile_material(m));assert not errors,errors
 assert lib.save_loaded_asset(m,False)
 instance=lib.load_asset(instance_path)
 created=instance is None
 if created:instance=u.AssetToolsHelpers.get_asset_tools().create_asset('MI_ArenaToothGrime','/Game/Gameplay/Care',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
 assert isinstance(instance,u.MaterialInstanceConstant)
 e.set_material_instance_parent(instance,m)
 names=['GrimePigmentSize','GrimePatternSeed','GrimeWarpStrength','GrimeOpacity','GrimeEdgeSoftness','GrimeEdgeErosion','GrimeWashOpacity','GrimeRoughness','GrimeMicroRelief','GrimeNormalStrength','GrimeDetailSize']
 if created:
  for name in names:e.set_material_instance_scalar_parameter_value(instance,name,ps[name].get_editor_property('default_value'))
  e.set_material_instance_vector_parameter_value(instance,'GrimeTint',tint.get_editor_property('default_value'))
  e.set_material_instance_texture_parameter_value(instance,'GrimePigment',ps['GrimePigment'].get_editor_property('texture'))
  # The asset preview needs a visible layer; gameplay supplies its own amount.
  e.set_material_instance_scalar_parameter_value(instance,'GrimeAmount',1.)
 e.update_material_instance(instance);assert lib.save_loaded_asset(instance,False)
 report=dict(parent=master_path,instance=instance_path,compile_errors=errors,created=created,artist_parameters={n:e.get_material_instance_scalar_parameter_value(instance,n) for n in names},runtime_parent='Every tooth creates its own dynamic instance inheriting this saved instance.')
 (out/'Instance.json').write_text(json.dumps(report,indent=2));print('PLAQUE_INSTANCE_CREATED',report)
