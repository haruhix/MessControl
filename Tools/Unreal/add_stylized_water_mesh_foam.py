"""Allow stylized foam on regular meshes without WaterBody data."""
import unreal as u,json
from pathlib import Path
from editor_toolset.toolsets.material import MaterialTools
E=u.MaterialEditingLibrary;A=u.EditorAssetLibrary
F='/Game/Art/Materials/'
foam=u.load_asset(F+'MF_Water_StylizedFoam')
nodes=MaterialTools._get_expressions(foam)
for n in list(nodes):
    if n.get_editor_property('material_expression_editor_y')>=2200:E.delete_material_expression_in_function(foam,n)
nodes=MaterialTools._get_expressions(foam)
step=next(n for n in nodes if isinstance(n,u.MaterialExpressionStep))
original=next(n for n in nodes if n.get_name()=='MaterialExpressionSaturate_8')
def node(cls,x,y,**v):
    n=E.create_material_expression_in_function(foam,cls,x,y);n.set_editor_properties(v);return n
def wire(a,b,pin,out=''):assert E.connect_material_expressions(a,out,b,pin)
depth=node(u.MaterialExpressionSceneDepthWithoutWater,0,2200,desc='Scene depth behind a regular water mesh')
pixel=node(u.MaterialExpressionPixelDepth,0,2380)
delta=node(u.MaterialExpressionSubtract,240,2200)
wire(depth,delta,'A');wire(pixel,delta,'B')
width=node(u.MaterialExpressionScalarParameter,0,2550,parameter_name='Foam Shore Width',default_value=110.,group='Stylized Foam',slider_min=1.,slider_max=300.)
divide=node(u.MaterialExpressionDivide,480,2200);wire(delta,divide,'A');wire(width,divide,'B')
inverse=node(u.MaterialExpressionOneMinus,690,2200);wire(divide,inverse,'')
clamp=node(u.MaterialExpressionSaturate,900,2200);wire(inverse,clamp,'')
toggle=node(u.MaterialExpressionStaticSwitchParameter,1120,2200,parameter_name='Regular Mesh Shore Foam',default_value=False,group='Stylized Foam')
wire(clamp,toggle,'True');wire(original,toggle,'False')
wire(toggle,step,'X')
E.update_material_function(foam)
mat=u.load_asset(F+'M_Water_Stylized')
errors=list(E.recompile_material(mat));assert not errors,errors
mi=u.load_asset(F+'MI_Water_Stylized_Plane')
E.set_material_instance_static_switch_parameter_value(mi,'Regular Mesh Shore Foam',True)
E.set_material_instance_scalar_parameter_value(mi,'Foam Shore Width',110.)
E.update_material_instance(mi)
for asset in [foam,mat,mi]:assert A.save_loaded_asset(asset,only_if_is_dirty=False)
print('REGULAR_MESH_SHORE_FOAM_SAVED',errors)
