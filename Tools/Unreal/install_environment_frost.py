"""Add matte fern frost to inspected map and prop materials in the open editor.

The existing MPC_MouthClimate.ColdAmount drives the effect. UV coordinates keep
the pattern attached to moving props. Preserve geometry, normals, opacity and
the tongue/player materials, which already have their own cold effects.
"""
import json
from pathlib import Path
import unreal as u

e, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
root = Path(u.Paths.project_dir())
out = root / 'Artifacts/EnvironmentFrost'
manifest = json.loads((out / 'Manifest.json').read_text(encoding='utf-8'))
folder = '/Game/Gameplay/Cold/Frost'
function_path = folder + '/MF_EnvironmentFrost'
texture = u.load_asset(folder + '/T_PlayerFrostMask')
climate = u.load_asset('/Game/Gameplay/Cold/MPC_MouthClimate')
assert isinstance(texture, u.Texture2D) and isinstance(climate, u.MaterialParameterCollection)
assert 'ColdAmount' in [str(n) for n in climate.get_scalar_parameter_names()]

def save(asset):
    assert lib.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_path_name()

def link(source, output, dest, input_name=''):
    assert e.connect_material_expressions(source, output, dest, input_name), input_name

function = u.load_asset(function_path) if lib.does_asset_exist(function_path) else None
if not function:
    function = u.AssetToolsHelpers.get_asset_tools().create_asset(
        'MF_EnvironmentFrost', folder, u.MaterialFunction, u.MaterialFunctionFactoryNew())
    assert function
if not e.get_material_function_expressions(function):
    function.set_editor_properties({'description':
        'Matte fern frost for map objects and props. Shared ColdAmount, stable UV0; no ice shell or displacement.',
        'expose_to_library': True})
    def node(cls, x, y, **properties):
        result = e.create_material_expression_in_function(function, cls, x, y)
        result.set_editor_properties(properties)
        return result
    def scalar(name, value, y, low=0.0, high=1.0):
        return node(u.MaterialExpressionScalarParameter, -1200, y,
            parameter_name=name, default_value=value, group='Environment Frost',
            slider_min=low, slider_max=high)
    def custom(description, code, inputs, x, y, scalar_output=False):
        result = node(u.MaterialExpressionCustom, x, y, description=description, code=code,
            output_type=u.CustomMaterialOutputType.CMOT_FLOAT1 if scalar_output else u.CustomMaterialOutputType.CMOT_FLOAT3)
        entries=[]
        for name in inputs:
            entry=u.CustomInput(); entry.set_editor_property('input_name', name); entries.append(entry)
        result.set_editor_property('inputs', entries)
        for name,(source,pin) in inputs.items(): link(source,pin,result,name)
        return result
    def preview(x,y=0,z=0,w=0):
        value=u.Vector4f()
        value.set_editor_properties({'x':x,'y':y,'z':z,'w':w})
        return value
    base={}
    for i,(name,scalar_input,preview) in enumerate([
            ('BaseColor',False,preview(.5,.5,.5,1)),
            ('Roughness',True,preview(.5)),
            ('Metallic',True,preview(0)),
            ('Specular',True,preview(.5))]):
        base[name]=node(u.MaterialExpressionFunctionInput,-1500,-800+i*210,
            input_name=name,input_type=u.FunctionInputType.FUNCTION_INPUT_SCALAR if scalar_input else u.FunctionInputType.FUNCTION_INPUT_VECTOR3,
            preview_value=preview,use_preview_value_as_default=True,sort_priority=i)
    cold=node(u.MaterialExpressionCollectionParameter,-1500,250,
        collection=climate,parameter_name='ColdAmount')
    amount=scalar('Environment Frost Amount',1,430)
    tiling=scalar('Environment Frost Tiling',2.5,600,.25,12)
    uv=node(u.MaterialExpressionTextureCoordinate,-1500,800,coordinate_index=0)
    scaled=node(u.MaterialExpressionMultiply,-960,800)
    link(uv,'',scaled,'A'); link(tiling,'',scaled,'B')
    sample=node(u.MaterialExpressionTextureSampleParameter2D,-720,650,
        parameter_name='Environment Frost Texture',group='Environment Frost',texture=texture,
        sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
        desc='Fern crystals in stable UV0; white branches build first during cold.')
    link(scaled,'',sample,'UVs')
    macro_uv=node(u.MaterialExpressionMultiply,-960,1070,const_b=.31)
    link(scaled,'',macro_uv,'A')
    macro=node(u.MaterialExpressionTextureSample,-720,1020,texture=texture,
        sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    link(macro_uv,'',macro,'UVs')
    coverage=custom('Matte environment frost coverage',
        'float a=saturate(Cold)*saturate(Amount);\n'
        'float crystal=smoothstep(lerp(0.90,0.025,a),lerp(0.98,0.16,a),saturate(Fern));\n'
        'float dust=pow(a,1.6)*(0.62+0.09*saturate(Macro));\n'
        'return saturate(dust+(1.0-dust)*crystal*0.94*smoothstep(0.0,0.15,a));',
        {'Cold':(cold,''),'Amount':(amount,''),'Fern':(sample,'R'),'Macro':(macro,'R')},-320,250,True)
    tint=node(u.MaterialExpressionVectorParameter,-450,650,
        parameter_name='Environment Frost Tint',group='Environment Frost',
        default_value=u.LinearColor(.73,.84,.91,1),desc='Diffuse frost, without glassy ice reflection.')
    color=custom('Environment frost diffuse crystals',
        'float variation=lerp(0.64,1.08,saturate(Fern))*lerp(0.92,1.04,saturate(Macro));\n'
        'return lerp(BaseColor,max(Tint,0.0)*variation,saturate(Coverage));',
        {'BaseColor':(base['BaseColor'],''),'Tint':(tint,''),'Fern':(sample,'R'),
         'Macro':(macro,'R'),'Coverage':(coverage,'')},150,-800)
    rough=custom('Environment frost dry roughness',
        'return lerp(BaseRoughness,lerp(0.87,0.97,saturate(Fern)),saturate(Coverage));',
        {'BaseRoughness':(base['Roughness'],''),'Fern':(sample,'R'),'Coverage':(coverage,'')},150,-560,True)
    metallic=custom('Environment frost nonmetallic surface',
        'return BaseMetallic*(1.0-saturate(Coverage));',
        {'BaseMetallic':(base['Metallic'],''),'Coverage':(coverage,'')},150,-320,True)
    specular=custom('Environment frost soft reflection',
        'return lerp(BaseSpecular,0.25,saturate(Coverage));',
        {'BaseSpecular':(base['Specular'],''),'Coverage':(coverage,'')},150,-80,True)
    for i,(name,source) in enumerate([('BaseColor',color),('Roughness',rough),
            ('Metallic',metallic),('Specular',specular),('Coverage',coverage)]):
        output=node(u.MaterialExpressionFunctionOutput,700,-800+i*230,
            output_name=name,sort_priority=i)
        link(source,'',output)
    e.update_material_function(function)
    save(function)

props=[('BaseColor',u.MaterialProperty.MP_BASE_COLOR),('Roughness',u.MaterialProperty.MP_ROUGHNESS),
       ('Metallic',u.MaterialProperty.MP_METALLIC),('Specular',u.MaterialProperty.MP_SPECULAR)]
protected=[u.MaterialProperty.MP_NORMAL,u.MaterialProperty.MP_WORLD_POSITION_OFFSET,
           u.MaterialProperty.MP_OPACITY_MASK,u.MaterialProperty.MP_FRONT_MATERIAL]
report={'function':function.get_path_name(),'driver':climate.get_path_name()+':ColdAmount',
        'mask':texture.get_path_name(),'materials':[]}
for entry in manifest['targets']:
    material=u.load_asset(entry['path'])
    assert isinstance(material,u.Material),entry['path']
    calls=[n for n in e.get_material_expressions(material) if isinstance(n,u.MaterialExpressionMaterialFunctionCall)
        and n.get_editor_property('material_function')==function]
    if calls:
        assert len(calls)==1
        backup_path=lib.get_metadata_tag(material,'EnvironmentFrost.Backup')
        report['materials'].append({'path':material.get_path_name(),'already_installed':True,
            'backup':backup_path,'connected_outputs':[name for name,prop in props
                if e.get_material_property_input_node(material,prop)==calls[0]],
            'protected_inputs_preserved':True,'compile_errors':[]})
        continue
    assert not e.get_material_property_input_node(material,u.MaterialProperty.MP_FRONT_MATERIAL)
    assert not material.get_editor_property('use_material_attributes')
    originals={name:(e.get_material_property_input_node(material,prop),
        str(e.get_material_property_input_node_output_name(material,prop))) for name,prop in props}
    assert originals['BaseColor'][0] and originals['Roughness'][0]
    for name,prop in props:
        expr,pin=originals[name]
        key={'BaseColor':'MP_BASE_COLOR','Roughness':'MP_ROUGHNESS',
             'Metallic':'MP_METALLIC','Specular':'MP_SPECULAR'}[name]
        before=entry['roots'][key]
        assert (expr.get_path_name() if expr else None)==before['expression'],material.get_path_name()+':'+name
        assert pin==before['output'],material.get_path_name()+':'+name
    protected_before=[(prop,e.get_material_property_input_node(material,prop),
        str(e.get_material_property_input_node_output_name(material,prop))) for prop in protected]
    backup_path=folder+'/EnvironmentBackups/'+material.get_name()+'_BeforeEnvironmentFrost'
    assert not lib.does_asset_exist(backup_path),'Backup already exists; inspect before changing '+backup_path
    backup=lib.duplicate_asset(material.get_path_name().split('.')[0],backup_path)
    assert backup; save(backup)
    with u.ScopedEditorTransaction('Add matte environment frost: '+material.get_name()):
        call=e.create_material_expression(material,u.MaterialExpressionMaterialFunctionCall,2600,1600)
        assert call.set_material_function(function)
        call.set_editor_property('desc','Shared matte fern frost; existing climate ColdAmount')
        connected=[]
        for name,prop in props:
            source,pin=originals[name]
            if source:
                link(source,pin,call,name)
                assert e.connect_material_property(call,name,prop)
                connected.append(name)
        errors=list(e.recompile_material(material))
        assert not errors,{'material':material.get_path_name(),'errors':errors}
        for prop,source,pin in protected_before:
            assert e.get_material_property_input_node(material,prop)==source
            assert str(e.get_material_property_input_node_output_name(material,prop))==pin
        lib.set_metadata_tag(material,'EnvironmentFrost.Function',function_path)
        lib.set_metadata_tag(material,'EnvironmentFrost.Backup',backup_path)
        save(material)
    row={'path':material.get_path_name(),'backup':backup.get_path_name(),
        'connected_outputs':connected,'compile_errors':errors,'protected_inputs_preserved':True}
    report['materials'].append(row)
    (out/'MaterialBuild.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('ENVIRONMENT_FROST_SAVED',material.get_path_name())
report['dirty_content']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
report['dirty_maps']=[p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
(out/'MaterialBuild.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
u.get_editor_subsystem(u.LevelEditorSubsystem).editor_invalidate_viewports()
print('ENVIRONMENT_FROST_INSTALL_COMPLETE',json.dumps(report))
