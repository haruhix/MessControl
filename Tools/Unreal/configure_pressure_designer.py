"""Install pressure material controls and example presets; preserve existing DA tuning.

Run after building the editor. Existing preset assets and material overrides are
never reset, so designers can keep editing them and safely rerun this installer.
"""
from pathlib import Path
import json
import unreal as u

lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
folder='/Game/Gameplay/Arena'

def save(asset):
    assert lib.save_loaded_asset(asset,only_if_is_dirty=False), asset.get_path_name()

def link(a,b,pin='',output=''):
    assert edit.connect_material_expressions(a,output,b,pin), 'Cannot connect '+pin

def existing(path): return lib.load_asset(path) if lib.does_asset_exist(path) else None

function=existing(folder+'/MF_TonguePressureSurface')
if not function:
    function=assets.create_asset('MF_TonguePressureSurface',folder,u.MaterialFunction,u.MaterialFunctionFactoryNew())
    function.set_editor_property('description','Pressure appearance from the shared physical surface. VertexColor G: depth / MaxDepth; B: rim slope; R remains reserved for pain. No displacement.')
    function.set_editor_property('expose_to_library',True)
    def node(kind,x,y): return edit.create_material_expression_in_function(function,kind,x,y)
    def scalar(name,value,y):
        n=node(u.MaterialExpressionScalarParameter,-800,y)
        n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',value)
        n.set_editor_property('group','Pressure | Appearance')
        n.set_editor_property('slider_min',0); n.set_editor_property('slider_max',1)
        return n
    def custom(description,code,inputs,x,y,scalar_output=False):
        n=node(u.MaterialExpressionCustom,x,y)
        n.set_editor_property('description',description); n.set_editor_property('code',code)
        n.set_editor_property('output_type',u.CustomMaterialOutputType.CMOT_FLOAT1 if scalar_output else u.CustomMaterialOutputType.CMOT_FLOAT3)
        entries=[]
        for name in inputs:
            entry=u.CustomInput(); entry.set_editor_property('input_name',name); entries.append(entry)
        n.set_editor_property('inputs',entries)
        for name,(source,output) in inputs.items(): link(source,n,name,output)
        return n
    base={}
    for i,name in enumerate(('BaseColor','Roughness','Normal')):
        n=node(u.MaterialExpressionFunctionInput,-1200,i*240)
        n.set_editor_property('input_name',name)
        n.set_editor_property('input_type',u.FunctionInputType.FUNCTION_INPUT_SCALAR if name=='Roughness' else u.FunctionInputType.FUNCTION_INPUT_VECTOR3)
        n.set_editor_property('sort_priority',i); base[name]=n
    vc=node(u.MaterialExpressionVertexColor,-1200,800)
    contrast=scalar('Pressure Contrast',.65,780)
    contrast.set_editor_property('slider_min',.2); contrast.set_editor_property('slider_max',4)
    feather=scalar('Pressure Feather',.12,880)
    feather.set_editor_property('slider_min',.01); feather.set_editor_property('slider_max',.4)
    mask=custom('MC pressure mask','float d=saturate(Depth); return pow(d,clamp(Contrast,0.2,4.0))*smoothstep(0,max(Feather,0.001),d);',{'Depth':(vc,'G'),'Contrast':(contrast,''),'Feather':(feather,'')},-500,800,True)
    tint=node(u.MaterialExpressionVectorParameter,-800,960)
    tint.set_editor_property('parameter_name','Pressure Tint'); tint.set_editor_property('default_value',u.LinearColor(.86,.76,.80,1))
    tint.set_editor_property('group','Pressure | Appearance')
    color=custom('MC pressure colour',
        'float3 c=BaseColor*lerp(float3(1,1,1),max(Tint,float3(0,0,0)),Mask*saturate(TintAmount));\nreturn max(c*(1-Mask*saturate(Darken))+BaseColor*saturate(Rim)*saturate(RimLight),float3(0,0,0));',
        {'BaseColor':(base['BaseColor'],''),'Mask':(mask,''),'Tint':(tint,''),
         'TintAmount':(scalar('Pressure Tint Amount',.45,1120),''),'Darken':(scalar('Pressure Darken',.12,1280),''),
         'Rim':(vc,'B'),'RimLight':(scalar('Pressure Rim Light',.08,1440),'')},100,0)
    rough=custom('MC pressure wetness','return clamp(lerp(BaseRoughness,WetRoughness,Mask*saturate(Wetness)),0.02,1.0);',
        {'BaseRoughness':(base['Roughness'],''),'Mask':(mask,''),'Wetness':(scalar('Pressure Wetness',.65,1600),''),
         'WetRoughness':(scalar('Pressure Roughness',.16,1760),'')},100,320,True)
    normal=custom('MC pressure micro normal','return normalize(float3(BaseNormal.xy*(1-Mask*saturate(Flatten)),BaseNormal.z));',
        {'BaseNormal':(base['Normal'],''),'Mask':(mask,''),'Flatten':(scalar('Pressure Normal Flatten',.15,1920),'')},100,600)
    for i,(name,source) in enumerate((('BaseColor',color),('Roughness',rough),('Normal',normal),('PressureMask',mask))):
        n=node(u.MaterialExpressionFunctionOutput,700,i*230)
        n.set_editor_property('output_name',name); n.set_editor_property('sort_priority',i); link(source,n)
    edit.update_material_function(function); save(function)

mat=lib.load_asset(folder+'/M_TonguePain')
assert not edit.get_material_property_input_node(mat,u.MaterialProperty.MP_WORLD_POSITION_OFFSET), 'Repair legacy WPO first'
base_color=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_BASE_COLOR)
if not (isinstance(base_color,u.MaterialExpressionMaterialFunctionCall) and base_color.get_editor_property('material_function')==function):
    originals=[]
    for name,prop in (('BaseColor',u.MaterialProperty.MP_BASE_COLOR),('Roughness',u.MaterialProperty.MP_ROUGHNESS),('Normal',u.MaterialProperty.MP_NORMAL)):
        original=edit.get_material_property_input_node(mat,prop)
        assert original, 'Expected artist '+name+' input'
        originals.append((name,prop,original,edit.get_material_property_input_node_output_name(mat,prop)))
    call=edit.create_material_expression(mat,u.MaterialExpressionMaterialFunctionCall,2200,1600)
    call.set_editor_property('desc','MC pressure appearance: shared geometry, no WPO')
    assert call.set_material_function(function)
    for name,prop,original,output in originals:
        link(original,call,name,output)
        assert edit.connect_material_property(call,name,prop)
    assert not edit.recompile_material(mat)
    save(mat)

base_mi=lib.load_asset(folder+'/MI_TonguePain')
root=lib.load_asset('/Game/Data/DA_Tongue')
before=tuple(root.pressure.to_tuple())
definitions=[
    ('Firm','Упругий','Небольшие вмятины, быстрое восстановление, сдержанный влажный след.',
     dict(max_depth=6,depth_per_kg=.3,falloff_power=3.5,press_seconds=.10,recover_seconds=.4,trail_hold_seconds=0,landing_boost=.6,landing_seconds=.16),
     {'Pressure Wetness':.35,'Pressure Darken':.07,'Pressure Rim Light':.05}),
    ('Soft','Мягкий','Широкий мягкий прогиб, читаемый влажный след и плавное восстановление.',
     dict(max_depth=18,depth_per_kg=.8,player_depth_scale=2,falloff_power=2.6,player_width_ratio=.85,press_seconds=.08,recover_seconds=2.2,trail_hold_seconds=.35,landing_boost=1.1,landing_seconds=.3),
     {'Pressure Contrast':.4,'Pressure Tint Amount':.9,'Pressure Wetness':.95,'Pressure Roughness':.10,'Pressure Darken':.12,'Pressure Rim Light':.10,'Pressure Normal Flatten':.4}),
    ('Deep','Вязкий','Глубокие следы от тяжёлой еды, дольше сохраняющаяся вмятина. Пресет для сравнения и настройки.',
     dict(max_depth=24,depth_per_kg=.85,player_depth_scale=2,falloff_power=2.2,player_width_ratio=.8,press_seconds=.10,recover_seconds=3,trail_hold_seconds=.5,landing_boost=1.3,landing_seconds=.45),
     {'Pressure Contrast':.4,'Pressure Tint Amount':.9,'Pressure Wetness':.95,'Pressure Roughness':.10,'Pressure Darken':.12,'Pressure Rim Light':.10,'Pressure Normal Flatten':.4})
]
presets=list(root.pressure_presets)
report=[]
for name,label,description,tuning,appearance in definitions:
    mi_path=folder+'/Pressure/MI_Pressure_'+name
    mi=existing(mi_path)
    if not mi:
        mi=lib.duplicate_asset(base_mi.get_path_name(),mi_path)
        for parameter,value in appearance.items():
            # UE 5.8's setter returns false even after successfully updating the value.
            edit.set_material_instance_scalar_parameter_value(mi,parameter,value)
            assert abs(edit.get_material_instance_scalar_parameter_value(mi,parameter)-value)<.0001, parameter
        if name in ('Soft','Deep'):
            edit.set_material_instance_vector_parameter_value(mi,'Pressure Tint',u.LinearColor(2.2,2.5,2.7,1))
        edit.update_material_instance(mi); save(mi)
    preset_path='/Game/Data/Pressure/DA_Pressure_'+name
    preset=existing(preset_path)
    if not preset:
        factory=u.DataAssetFactory(); factory.set_editor_property('data_asset_class',u.MCTonguePressurePreset)
        preset=assets.create_asset('DA_Pressure_'+name,'/Game/Data/Pressure',u.MCTonguePressurePreset,factory)
        preset.set_editor_property('label',u.Text(label)); preset.set_editor_property('description',u.Text(description))
        settings=preset.settings
        for key,value in tuning.items(): settings.set_editor_property(key,value)
        preset.set_editor_property('settings',settings); preset.set_editor_property('surface_material',mi); save(preset)
    if preset not in presets: presets.append(preset)
    report.append(dict(preset=preset.get_path_name(),material=mi.get_path_name(),settings=str(preset.settings)))
root.set_editor_property('pressure_presets',presets)
assert tuple(root.pressure.to_tuple())==before, 'Inline artist tuning changed'
save(root)
out=Path(u.Paths.project_saved_dir())/'PressureDesignerReview'
out.mkdir(parents=True,exist_ok=True)
(out/'assets.json').write_text(json.dumps(dict(preserved_inline=before,presets=report),ensure_ascii=False,indent=2),encoding='utf-8')
u.log('MC_PRESSURE_DESIGNER_ASSETS_PASS '+str(out/'assets.json'))
u.SystemLibrary.quit_editor()
