"""Author the airborne goo and impact spread on the existing wipeable vomit material."""
import json
from pathlib import Path
import unreal as u

lib=u.EditorAssetLibrary
edit=u.MaterialEditingLibrary
assets=u.AssetToolsHelpers.get_asset_tools()
folder='/Game/Gameplay/Hazards'
path=folder+'/M_VomitMass'
created=not lib.does_asset_exist(path)
mat=lib.load_asset(path) if not created else assets.create_asset('M_VomitMass',folder,u.Material,u.MaterialFactoryNew())
if created:
    mat.set_editor_property('blend_mode',u.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property('two_sided',False)
    mat.set_editor_property('used_with_instanced_static_meshes',True)

    def node(kind,x,y): return edit.create_material_expression(mat,kind,x,y)
    def vector(name,value,y):
        n=node(u.MaterialExpressionVectorParameter,-800,y)
        n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',u.LinearColor(*value))
        return n
    def custom(description,code,inputs,x,y,output):
        n=node(u.MaterialExpressionCustom,x,y)
        n.set_editor_property('description',description); n.set_editor_property('code',code); n.set_editor_property('output_type',output)
        entries=[]
        for name in inputs:
            item=u.CustomInput(); item.set_editor_property('input_name',name); entries.append(item)
        n.set_editor_property('inputs',entries)
        for name,(source,pin) in inputs.items(): assert edit.connect_material_expressions(source,pin,n,name)
        return n
    def prop(src,kind): assert edit.connect_material_property(src,'',kind)

    wp=node(u.MaterialExpressionWorldPosition,-1000,-100)
    uv=node(u.MaterialExpressionTextureCoordinate,-1000,300)
    field=custom('Wet goo with suspended food and pale aeration',r'''
    float3 p=P*.027;
    float fold=sin(p.x+sin(p.y*1.4))*sin(p.z*1.6+p.y*.8);
    float n=sin(p.x*3.4+sin(p.z*2.1))*cos(p.y*3.8+sin(p.x*2.9));
    float chunks=smoothstep(.42,.84,n);
    float foam=smoothstep(.86,.98,sin(p.x*9.1+p.z*7.3)*sin(p.y*8.7-p.z*6.2));
    float3 color=lerp(Body,Dark,saturate(.22+fold*.20+chunks*.6));
    color=lerp(color,Foam,foam*.65);
    return float4(color,.18+chunks*.10+foam*.12);
    ''',{'P':(wp,''),'Body':(vector('BodyColor',(.28,.16,.025,1),0),''),
         'Dark':(vector('ChunkColor',(.105,.064,.014,1),150),''),'Foam':(vector('FoamColor',(.62,.44,.14,1),300),'')},-300,0,u.CustomMaterialOutputType.CMOT_FLOAT4)
    prop(custom('Body','return F.rgb;',{'F':(field,'')},0,0,u.CustomMaterialOutputType.CMOT_FLOAT3),u.MaterialProperty.MP_BASE_COLOR)
    prop(custom('Wet surface','return F.a;',{'F':(field,'')},0,200,u.CustomMaterialOutputType.CMOT_FLOAT1),u.MaterialProperty.MP_ROUGHNESS)
    prop(custom('Fine viscous skin','return normalize(float3(sin(UV.x*55+sin(UV.y*33))*.045,cos(UV.y*47)*.045,1));',{'UV':(uv,'')},0,400,u.CustomMaterialOutputType.CMOT_FLOAT3),u.MaterialProperty.MP_NORMAL)
    prop(custom('Mouth ambient fill','return F.rgb*.08;',{'F':(field,'')},0,600,u.CustomMaterialOutputType.CMOT_FLOAT3),u.MaterialProperty.MP_EMISSIVE_COLOR)
    spec=node(u.MaterialExpressionConstant,0,800); spec.set_editor_property('r',.78); prop(spec,u.MaterialProperty.MP_SPECULAR)
    errors=edit.recompile_material(mat); assert not errors,errors
    assert lib.save_loaded_asset(mat,False)

mat.set_editor_property('used_with_instanced_static_meshes',True)
errors=edit.recompile_material(mat); assert not errors,errors
assert lib.save_loaded_asset(mat,False)

puddle=lib.load_asset(folder+'/M_VomitPuddle'); assert puddle
expressions=list(edit.get_material_expressions(puddle))
arrival=next((n for n in expressions if isinstance(n,u.MaterialExpressionScalarParameter) and str(n.get_editor_property('parameter_name'))=='Arrival'),None)
if arrival is None:
    arrival=edit.create_material_expression(puddle,u.MaterialExpressionScalarParameter,-1200,-160)
    arrival.set_editor_property('parameter_name','Arrival'); arrival.set_editor_property('default_value',1)
for n in expressions:
    if not isinstance(n,u.MaterialExpressionCustom): continue
    if str(n.get_editor_property('description')) not in ['Liquid height, coverage, meniscus, thickness','Rounded liquid edge and brush wake normals']: continue
    inputs=list(n.get_editor_property('inputs'))
    if not any(str(i.get_editor_property('input_name'))=='Arrival' for i in inputs):
        item=u.CustomInput(); item.set_editor_property('input_name','Arrival'); inputs.append(item); n.set_editor_property('inputs',inputs)
    assert edit.connect_material_expressions(arrival,'',n,'Arrival')
    code=n.get_editor_property('code')
    if '// Impact spread' not in code:
        code=code.replace('CoffeeSurface S;','CoffeeSurface S;\n// Impact spread: the final seeded outline and wipe UVs remain unchanged.\nUV=.5+(UV-.5)/lerp(.22,1,saturate(Arrival));')
        n.set_editor_property('code',code)
errors=edit.recompile_material(puddle); assert not errors,errors
assert lib.save_loaded_asset(puddle,False)
mi=lib.load_asset(folder+'/MI_VomitPuddle'); edit.update_material_instance(mi); assert lib.save_loaded_asset(mi,False)
Path(u.Paths.project_saved_dir(),'VomitMaterials.json').write_text(json.dumps({'airborne':mat.get_path_name(),'puddle':puddle.get_path_name(),'arrival_default':1,'compiled':True},indent=2),encoding='utf-8')
u.log('MC_VOMIT_MATERIALS_PASS')
