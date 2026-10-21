"""Create project-owned stylized UE Water material from GraphicLine's reference."""
import json
from pathlib import Path
import unreal as u
from editor_toolset.toolsets.material import MaterialTools

E=u.MaterialEditingLibrary
A=u.EditorAssetLibrary
F='/Game/Art/Materials'
OUT=Path(u.Paths.project_dir())/'Saved/StylizedWater_20261021'
OUT.mkdir(parents=True,exist_ok=True)

def copy(source,name):
    path=F+'/'+name
    assert not A.does_asset_exist(path), 'Already exists: '+path
    asset=A.duplicate_asset(source,path)
    assert asset,path
    return asset

def expressions(asset):return MaterialTools._get_expressions(asset)
def node(asset,cls,x,y,**props):
    n=(E.create_material_expression(asset,cls,x,y) if isinstance(asset,u.Material)
       else E.create_material_expression_in_function(asset,cls,x,y))
    n.set_editor_properties(props)
    return n
def wire(src,dst,pin,out=''):
    assert E.connect_material_expressions(src,out,dst,pin),(src,dst,pin,out)
def scalar(asset,name,value,x,y):
    return node(asset,u.MaterialExpressionScalarParameter,x,y,parameter_name=name,
                default_value=value,group='Stylized Foam',slider_min=0.0,slider_max=1.0)

foam=copy('/Water/Materials/Layers/BeachFoam','MF_Water_StylizedFoam')
mat=copy('/Water/Materials/WaterSurface/Water_Material','M_Water_Stylized')
foam_nodes=expressions(foam)
mask=next(n for n in foam_nodes if n.get_name()=='MaterialExpressionSaturate_8')
consumers=[]
for n in foam_nodes:
    for src in MaterialTools.get_expression_inputs(foam,n):
        if src.expression==mask:consumers.append((n,str(src.input_name)))
threshold=scalar(foam,'Foam Threshold',0.42,850,1030)
threshold.set_editor_property('desc','Lower values give wider solid-white foam. Reference: GraphicLine stylized water, 2026-04-17.')
step=node(foam,u.MaterialExpressionStep,1120,1040,desc='Hard cartoon foam boundary')
pins=[str(p) for p in E.get_material_expression_input_names(step)]
assert set(pins)=={'X','Y'},pins
wire(threshold,step,'Y');wire(mask,step,'X')
for n,pin in consumers:wire(step,n,pin)

# Keep the original moving foam textures, distance/depth detection, normals and
# scattering. Only quantize their combined foam mask, then control its shading.
emissive=next(n for n in foam_nodes if n.get_name()=='MaterialExpressionMultiply_17')
shade=scalar(foam,'Foam Shading',0.32,1540,-420)
mul=node(foam,u.MaterialExpressionMultiply,1810,-260,desc='Soft white foam shading')
wire(emissive,mul,'A');wire(shade,mul,'B')
for n in foam_nodes:
    if n==emissive:continue
    for src in MaterialTools.get_expression_inputs(foam,n):
        if src.expression==emissive:wire(mul,n,str(src.input_name))

replaced=[]
for n in expressions(mat):
    if isinstance(n,u.MaterialExpressionMaterialFunctionCall):
        function=n.get_editor_property('material_function')
        if function and function.get_path_name()=='/Water/Materials/Layers/BeachFoam.BeachFoam':
            n.set_editor_property('material_function',foam);replaced.append(n.get_name())
    if isinstance(n,u.MaterialExpressionStaticSwitchParameter):
        if str(n.get_editor_property('parameter_name'))=='Enable Ocean Foam':n.set_editor_property('default_value',True)
assert replaced
E.update_material_function(foam)
errors=list(E.recompile_material(mat))
assert not errors,errors

factory=u.MaterialInstanceConstantFactoryNew()
tools=u.AssetToolsHelpers.get_asset_tools()
mi=tools.create_asset('MI_Water_Stylized',F,u.MaterialInstanceConstant,factory)
assert mi
E.set_material_instance_parent(mi,mat)

scalars={'Water Roughness':0.20,'Water Specular':0.42,'Water Fresnel Roughness':0.14,
 'Water Fresnel Specular':0.38,'Foam Threshold':0.42,'Foam Shading':0.32,
 'Foam Distance':95.0,'Depth for DF Foam':80.0,'Foam Dpeth Min':65.0,
 'Foam Opacity':1.0,'Foam Roughness':0.62,'FoamContrast':0.75,
 'Foam MacroScale':900.0,'Front Foam Scale':420.0,'Foam Normal I':0.15,
 'Foam Boost':1.15,'Height Bias':-0.6,'Front Height Bias':-0.35,
 'Mask Wave Foam near FluidSim':0.0,'Default Near Water Scale':900.0,
 'Default Near Normal Strength':0.65,'Default Distant Water Scale':1800.0,
 'Default Disant Water Speed':0.075,'Default Distant Normal Strength':0.50,
 'Default Distant Normal StrengthB':0.32,'Refraction':1.025}
vectors={'Water Albedo':u.LinearColor(0.012,0.19,0.21,1),
 'Scattering':u.LinearColor(0.055,0.68,0.58,1.8),
 'Absorption':u.LinearColor(7.0,95.0,145.0,1.0),
 'Foam Emissive':u.LinearColor(0.91,0.99,1.0,1.0),
 'Foam Scattering':u.LinearColor(0.92,0.99,1.0,7.0)}
known={str(p) for p in E.get_scalar_parameter_names(mat)}
assert not(set(scalars)-known),set(scalars)-known
for name,value in scalars.items():
    E.set_material_instance_scalar_parameter_value(mi,name,value)
    assert abs(E.get_material_instance_scalar_parameter_value(mi,name)-value)<0.001,name
for name,value in vectors.items():E.set_material_instance_vector_parameter_value(mi,name,value)
E.set_material_instance_static_switch_parameter_value(mi,'Enable Ocean Foam',True)
E.update_material_instance(mi)

# An ordinary mesh can use this instance without a Water Body actor or its data.
plane=tools.create_asset('MI_Water_Stylized_Plane',F,u.MaterialInstanceConstant,factory)
assert plane
E.set_material_instance_parent(plane,mi)
switches={'Enable Waves':False,'Enable Water VS Mapping':False,'UseFixedWaterDepth':True,
 'UseFixedZ':True,'UseWaterInfoTexture':False,'DisableDynamicBoundsClip':True,'UseFixedVelocity':True}
for name,value in switches.items():E.set_material_instance_static_switch_parameter_value(plane,name,value)
E.update_material_instance(plane)

paths=[]
for asset in [foam,mat,mi,plane]:
    A.set_metadata_tag(asset,'Water.Reference','https://www.youtube.com/watch?v=ysOSGKFX4CA')
    A.set_metadata_tag(asset,'Water.Note','Project-owned stylized foam copy; original engine Water assets preserved.')
    assert A.save_loaded_asset(asset,only_if_is_dirty=False)
    paths.append(asset.get_path_name())
report={'assets':paths,'compile_errors':errors,'foam_mask_consumers':[(n.get_name(),pin) for n,pin in consumers],
 'remapped_functions':replaced,'scalars':scalars,'plane_switches':switches,
 'reference':'https://www.youtube.com/watch?v=ysOSGKFX4CA',
 'source':'/Water/Materials/WaterSurface/Water_Material','stats':str(E.get_statistics(mat))}
(OUT/'authoring.json').write_text(json.dumps(report,indent=2))
print('STYLIZED_WATER_SAVED '+json.dumps({'assets':paths,'compile_errors':errors}))
