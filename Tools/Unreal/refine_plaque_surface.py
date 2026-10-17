"""Reference-aligned bump, editable opacity mask and bounded surface separation.

Apply after create_plaque_material_instance.py. Existing instance values survive.
"""
import unreal as u,json
from pathlib import Path
root=Path(u.Paths.project_dir());out=root/'Artifacts/GrimeSurface_20261017';out.mkdir(exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
e=u.MaterialEditingLibrary;lib=u.EditorAssetLibrary
m=lib.load_asset('/Game/Gameplay/Care/M_ArenaToothRelief');mi=lib.load_asset('/Game/Gameplay/Care/MI_ArenaToothGrime')
nodes=list(e.get_material_expressions(m));cs={str(n.get_editor_property('description')):n for n in nodes if isinstance(n,u.MaterialExpressionCustom)}
ps={str(n.get_editor_property('parameter_name')):n for n in nodes if isinstance(n,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter,u.MaterialExpressionTextureObjectParameter))}
added=[]
def scalar(name,value,group,order,desc,limits):
 n=ps.get(name)
 if n is None:
  n=e.create_material_expression(m,u.MaterialExpressionScalarParameter,1600,6000+len(ps)*80);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',value);ps[name]=n;added.append(name)
 n.set_editor_properties(dict(group=group,sort_priority=order,desc=desc,slider_min=limits[0],slider_max=limits[1]));return n
def inputs(node,sources):
 entries=list(node.get_editor_property('inputs'));names={str(x.get_editor_property('input_name')) for x in entries}
 for key in sources:
  if key not in names:
   v=u.CustomInput();v.set_editor_property('input_name',key);entries.append(v)
 node.set_editor_property('inputs',entries)
 for key,src in sources.items():assert e.connect_material_expressions(src,'',node,key),key
def custom(label,code,sources,kind):
 n=cs.get(label)
 if n is None:n=e.create_material_expression(m,u.MaterialExpressionCustom,2300,6000+len(cs)*160);n.set_editor_property('description',label);cs[label]=n
 n.set_editor_properties(dict(code=code,output_type=kind));inputs(n,sources);return n
with u.ScopedEditorTransaction('Plaque normal, opacity mask and surface fit controls'):
 threshold=scalar('GrimeMaskThreshold',.12,'02 Opacity Mask',1,'Порог маски. Больше значение — меньше налёта.',(0.,1.))
 softness=scalar('GrimeMaskSoftness',.40,'02 Opacity Mask',2,'Плавность границ внутри рисунка. Малое значение делает край резче.',(.01,1.))
 contrast=scalar('GrimeMaskContrast',1.,'02 Opacity Mask',3,'Контраст маски прозрачности.',(.1,4.))
 strength=scalar('GrimeMaskStrength',.75,'02 Opacity Mask',4,'Влияние рисунка на прозрачность. Ноль — сплошной слой внутри внешнего контура.',(0.,1.))
 invert=scalar('GrimeMaskInvert',1.,'02 Opacity Mask',5,'1: тёмные пятна непрозрачны. 0: белое на маске непрозрачно.',(0.,1.))
 texture_mix=scalar('GrimeMaskTextureStrength',0.,'02 Opacity Mask',6,'0: маска из цветного рисунка. 1: отдельная GrimeOpacityMask, с тем же случайным наложением.',(0.,1.))
 ps['GrimeOpacity'].set_editor_properties(dict(group='02 Opacity Mask',sort_priority=0))
 mask_tex=ps.get('GrimeOpacityMask')
 if mask_tex is None:
  mask_tex=e.create_material_expression(m,u.MaterialExpressionTextureObjectParameter,1600,5800);mask_tex.set_editor_properties(dict(parameter_name='GrimeOpacityMask',texture=e.get_material_instance_texture_parameter_value(mi,'GrimePigment'),sampler_type=ps['GrimePigment'].get_editor_property('sampler_type')));ps['GrimeOpacityMask']=mask_tex
 mask_tex.set_editor_properties(dict(group='02 Opacity Mask',sort_priority=7,desc='Необязательная текстура маски. Включается параметром GrimeMaskTextureStrength; масштаб, seed и warp общие с цветом.'))
 normal_all=scalar('GrimeNormalIntensity',1.,'04 Normal',0,'Общая сила нормали. Ноль полностью отключает рельеф.',(0.,3.))
 pigment_relief=scalar('GrimePigmentRelief',.65,'04 Normal',1,'Высота рельефа пятен в сантиметрах. Рисунок и рандомизация совпадают с цветом.',(0.,3.))
 scalar('GrimeNormalStrength',.12,'04 Normal',2,'Сила мелкой Normal Map GrimeNormal.',(0.,2.))
 scalar('GrimeMicroRelief',.13,'04 Normal',3,'Мелкий рельеф Height Map.',(0.,2.))
 ps['GrimeNormal'].set_editor_properties(dict(group='04 Normal',sort_priority=4,desc='Дополнительная карта нормалей мелкого зерна.'))
 ps['GrimeHeight'].set_editor_properties(dict(group='04 Normal',sort_priority=5))
 offset=scalar('GrimeSurfaceOffset',0.,'05 Surface Fit',0,'Дополнительный отступ от эмали в сантиметрах. Убирает пересечение поверхности.',(0.,3.))
 # Optional independent mask follows exactly the same stochastic projection as
 # pigment. Its texture lookups are skipped when the blend is disabled.
 pigment=cs['MC Care: anchored plaque detail pigment']
 code=pigment.get_editor_property('code').replace('[branch] if(Amount<=.0001) return float3(.82,.66,.36);','[branch] if(Amount<=.0001 || TextureStrength<=.0001) return 1;')
 assert code.rstrip().endswith('return color;')
 code=code.rstrip()[:-len('return color;')]+'return dot(color,float3(.2126,.7152,.0722));\n'
 src={str(i.get_editor_property('input_name')):s for i,s in zip(pigment.get_editor_property('inputs'),e.get_inputs_for_material_expression(m,pigment))}
 src['Pigment']=mask_tex;src['TextureStrength']=texture_mix
 mask=custom('MC Care: artist opacity texture',code,src,u.CustomMaterialOutputType.CMOT_FLOAT1)
 clip=cs['MC Care: coatingClip'];inputs(clip,dict(MaskThreshold=threshold,MaskSoftness=softness,MaskContrast=contrast,MaskStrength=strength,MaskInvert=invert,MaskTextureStrength=texture_mix,UserMask=mask))
 clip_code=clip.get_editor_property('code')
 old='return saturate(feather*paper*wipe)*saturate(Opacity);'
 new='''// Artist texture/pigment mask is separate from the authoritative brush wipe.
 float raw=lerp(dot(Pigment,float3(.2126,.7152,.0722)),UserMask,saturate(MaskTextureStrength));
 raw=lerp(raw,1-raw,saturate(MaskInvert));
 raw=saturate((raw-.5)*max(MaskContrast,.01)+.5);
 float artwork=smoothstep(saturate(MaskThreshold),saturate(MaskThreshold)+max(MaskSoftness,.001),raw);
 float alpha=saturate(feather*paper*wipe)*saturate(Opacity)*lerp(1,artwork,saturate(MaskStrength));
 return alpha;'''
 if old in clip_code:clip.set_editor_property('code',clip_code.replace(old,new))
 else:assert 'float artwork=' in clip_code
 normal=cs['MC Care: meniscusNormal'];inputs(normal,dict(Pigment=pigment,PigmentRelief=pigment_relief,NormalIntensity=normal_all,Alpha=clip))
 normal.set_editor_property('code',r'''
 float3 n=normalize(N);
 // Never differentiate vertex coverage or the cleaning volume. Their linear
 // interpolation used to print triangle/volume boundaries into the lighting.
 float relief=pow(saturate(1-dot(Pigment,float3(.2126,.7152,.0722))),1.35);
 float h=D.w*max(Strength,0)+relief*max(PigmentRelief,0);
 float3 dx=ddx_fine(World),dy=ddy_fine(World),rx=cross(dy,n),ry=cross(n,dx);
 float det=dot(dx,rx);
 float sx=ddx_fine(h)+max(NormalStrength,0)*dot(Slope,ddx_fine(P*Scale));
 float sy=ddy_fine(h)+max(NormalStrength,0)*dot(Slope,ddy_fine(P*Scale));
 float3 gradient=(sx*rx+sy*ry)*sign(det)/max(abs(det),1e-5);
 gradient*=max(NormalIntensity,0)*smoothstep(0,.25,Alpha);
 gradient*=min(1,.55/max(length(gradient),1e-4));
 return normalize(n-gradient);
''')
 vertex_normal=next(n for n in nodes if isinstance(n,u.MaterialExpressionVertexNormalWS))
 lift=custom('MC Care: enamel separation','return normalize(N)*clamp(Offset,0,3);',dict(N=vertex_normal,Offset=offset),u.CustomMaterialOutputType.CMOT_FLOAT3)
 assert e.connect_material_property(lift,'',u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
 assert e.connect_material_property(normal,'',u.MaterialProperty.MP_NORMAL)
 assert e.connect_material_property(clip,'',u.MaterialProperty.MP_OPACITY)
 m.set_editor_property('tangent_space_normal',False)
 errors=list(e.recompile_material(m));assert not errors,errors
 for name in added:e.set_material_instance_scalar_parameter_value(mi,name,ps[name].get_editor_property('default_value'))
 # Explicit existing controls stay enabled and retain the user's in-memory values.
 for name in ['GrimeNormalStrength','GrimeMicroRelief']:
  value=e.get_material_instance_scalar_parameter_value(mi,name);e.set_material_instance_scalar_parameter_value(mi,name,value)
 if 'GrimeOpacityMask' not in [str(v.get_editor_property('parameter_info').get_editor_property('name')) for v in mi.get_editor_property('texture_parameter_values')]:e.set_material_instance_texture_parameter_value(mi,'GrimeOpacityMask',mask_tex.get_editor_property('texture'))
 e.update_material_instance(mi)
 assert lib.save_loaded_asset(m,False);assert lib.save_loaded_asset(mi,False)
 report=dict(compile_errors=errors,added=added,values={name:e.get_material_instance_scalar_parameter_value(mi,name) for name,n in ps.items() if isinstance(n,u.MaterialExpressionScalarParameter)})
 (out/'Material.json').write_text(json.dumps(report,indent=2));print('PLAQUE_SURFACE_SAVED',report)
