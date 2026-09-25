"""Small graph authoring helper used only by the reproducible coffee asset generator."""
import unreal as u

class Graph:
    def __init__(self,name,translucent=True,world_normal=False):
        self.lib=u.MaterialEditingLibrary; self.assets=u.AssetToolsHelpers.get_asset_tools(); self.row=0
        self.path='/Game/Art/Materials/'+name
        self.mat=u.EditorAssetLibrary.load_asset(self.path) if u.EditorAssetLibrary.does_asset_exist(self.path) else self.assets.create_asset(name,'/Game/Art/Materials',u.Material,u.MaterialFactoryNew())
        # UE 5.8's bulk helper iterates the same array it removes from, leaving
        # stale nodes/duplicate parameter names. Delete a stable snapshot instead.
        for expression in list(self.lib.get_material_expressions(self.mat)):
            self.lib.delete_material_expression(self.mat,expression)
        if self.lib.get_num_material_expressions(self.mat):
            raise RuntimeError('Material graph did not clear: '+self.path)
        self.mat.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT if translucent else u.BlendMode.BLEND_OPAQUE)
        self.mat.set_editor_property('two_sided',True)
        self.mat.set_editor_property('tangent_space_normal',not world_normal)
        if translucent:
            self.mat.set_editor_property('translucency_lighting_mode',u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
            self.mat.set_editor_property('screen_space_reflections',True)
            self.mat.set_editor_property('refraction_method',u.RefractionMode.RM_PIXEL_NORMAL_OFFSET)
    def node(self,cls,x=-1100):
        self.row+=85
        return self.lib.create_material_expression(self.mat,cls,x,self.row)
    def scalar(self,name,value,group='Appearance'):
        n=self.node(u.MaterialExpressionScalarParameter)
        n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',value); n.set_editor_property('group',group)
        return n
    def vector(self,name,value,group='Appearance'):
        n=self.node(u.MaterialExpressionVectorParameter,-1450)
        n.set_editor_property('parameter_name',name); n.set_editor_property('default_value',u.LinearColor(*value)); n.set_editor_property('group',group)
        return n
    def link(self,a,b,pin='',output=None):
        if output is None: output='RGBA' if isinstance(a,u.MaterialExpressionVectorParameter) else ''
        if not self.lib.connect_material_expressions(a,output,b,pin):
            raise RuntimeError('Cannot connect material input '+pin)
    def custom(self,name,code,inputs,dim=1):
        n=self.node(u.MaterialExpressionCustom,100)
        n.set_editor_property('description',name); n.set_editor_property('code',code)
        n.set_editor_property('output_type',{1:u.CustomMaterialOutputType.CMOT_FLOAT1,2:u.CustomMaterialOutputType.CMOT_FLOAT2,3:u.CustomMaterialOutputType.CMOT_FLOAT3}[dim])
        entries=[]
        for key in inputs:
            p=u.CustomInput(); p.set_editor_property('input_name',key); entries.append(p)
        n.set_editor_property('inputs',entries)
        for key,value in inputs.items(): self.link(value,n,key)
        return n
    def output(self,node,prop):
        if not self.lib.connect_material_property(node,'',prop):
            raise RuntimeError('Cannot connect material output '+str(prop))
    def depth(self,name,distance):
        n=self.node(u.MaterialExpressionDepthFade); n.set_editor_property('fade_distance_default',distance)
        self.link(self.scalar(name,distance),n,'FadeDistance'); return n
    def save(self,instance):
        names=[]
        for expression in self.lib.get_material_expressions(self.mat):
            if isinstance(expression,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter)):
                name=str(expression.get_editor_property('parameter_name'))
                if name in names: raise RuntimeError('Duplicate material parameter '+name)
                names.append(name)
        errors=self.lib.recompile_material(self.mat)
        if errors: raise RuntimeError(str(errors))
        if not u.EditorAssetLibrary.save_loaded_asset(self.mat,only_if_is_dirty=False):
            raise RuntimeError('Cannot save '+self.path)
        path='/Game/Art/Materials/'+instance
        mi=u.EditorAssetLibrary.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else None
        if not mi:
            mi=self.assets.create_asset(instance,'/Game/Art/Materials',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
        # A graph rebuild can change uniform parameter layout. Refresh the
        # existing instance's parent data as well as its shader resource.
        self.lib.set_material_instance_parent(mi,None)
        self.lib.set_material_instance_parent(mi,self.mat)
        self.lib.update_material_instance(mi)
        if not u.EditorAssetLibrary.save_loaded_asset(mi,only_if_is_dirty=False):
            raise RuntimeError('Cannot save '+path)
        return mi
