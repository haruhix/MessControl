"""Give foam a white lit base color while preserving the water attributes."""
import unreal as u
from editor_toolset.toolsets.material import MaterialTools
E=u.MaterialEditingLibrary;A=u.EditorAssetLibrary;F='/Game/Art/Materials/'
foam=u.load_asset(F+'MF_Water_StylizedFoam')
nodes=MaterialTools._get_expressions(foam)
existing=[n for n in nodes if isinstance(n,u.MaterialExpressionSetMaterialAttributes) and str(n.get_editor_property('desc'))=='White foam base color; preserve original water attributes']
if existing and 'BaseColor' in [str(p) for p in E.get_material_expression_input_names(existing[0])]:
    print('WHITE_LIT_FOAM_ALREADY_PRESENT')
else:
    attrs=next(n for n in nodes if isinstance(n,u.MaterialExpressionSetMaterialAttributes))
    output=next(n for n in nodes if isinstance(n,u.MaterialExpressionFunctionOutput) and str(n.get_editor_property('output_name'))=='Attributes')
    step=next(n for n in nodes if isinstance(n,u.MaterialExpressionStep))
    def node(cls,x,y,**v):
        n=E.create_material_expression_in_function(foam,cls,x,y);n.set_editor_properties(v);return n
    def wire(a,b,pin,out=''):assert E.connect_material_expressions(a,out,b,pin),(pin,out)
    template_function=u.load_asset('/Water/Materials/Layers/WaterAttributes')
    template=next(n for n in MaterialTools._get_expressions(template_function) if isinstance(n,u.MaterialExpressionSetMaterialAttributes))
    finish=E.duplicate_material_expression(None,foam,template)
    finish.set_editor_property('desc','White foam base color; preserve original water attributes')
    finish.set_editor_property('material_expression_editor_x',3000)
    finish.set_editor_property('material_expression_editor_y',-100)
    for pin in E.get_material_expression_input_names(finish):E.disconnect_material_expressions(finish,str(pin))
    wire(attrs,finish,'MaterialAttributes')
    water=node(u.MaterialExpressionVectorParameter,2400,1800,parameter_name='Water Albedo',default_value=u.LinearColor(0.012,0.19,0.21,1),group='Water')
    color=node(u.MaterialExpressionVectorParameter,2400,1600,parameter_name='Foam Color',default_value=u.LinearColor(0.88,0.98,1.,1.),group='Stylized Foam')
    blend=node(u.MaterialExpressionLinearInterpolate,2750,1400,desc='White lit foam over water albedo')
    wire(water,blend,'A','RGB');wire(color,blend,'B','RGB');wire(step,blend,'Alpha')
    wire(blend,finish,'BaseColor');wire(finish,output,'')
    E.update_material_function(foam)
    mat=u.load_asset(F+'M_Water_Stylized');errors=list(E.recompile_material(mat));assert not errors,errors
    for name in ['MI_Water_Stylized','MI_Water_Stylized_Plane']:E.update_material_instance(u.load_asset(F+name))
    for asset in [foam,mat,u.load_asset(F+'MI_Water_Stylized'),u.load_asset(F+'MI_Water_Stylized_Plane')]:assert A.save_loaded_asset(asset,only_if_is_dirty=False)
    print('WHITE_LIT_FOAM_SAVED',errors)
