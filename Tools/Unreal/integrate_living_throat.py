"""Import mouth tissue and uvula, then replace the legacy instant-disposal marker."""
import unreal as u, sys, json
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve(); sys.path.insert(0,str(root/'Tools/Unreal'))
from coffee_material_graph import Graph
lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary; assets=u.AssetToolsHelpers.get_asset_tools()
actors=u.get_editor_subsystem(u.EditorActorSubsystem); levels=u.get_editor_subsystem(u.LevelEditorSubsystem)
def save(a):
    if not lib.save_loaded_asset(a,only_if_is_dirty=False): raise RuntimeError('Save failed: '+a.get_path_name())
folder='/Game/Gameplay/Throat'
for file in (root/'ArtSource/LivingThroat/Textures').glob('*.png'):
    task=u.AssetImportTask(); task.filename=str(file); task.destination_path=folder+'/Textures'; task.automated=True; task.save=True; task.replace_existing=True
    assets.import_asset_tasks([task])
    tex=lib.load_asset(folder+'/Textures/'+file.stem)
    if 'Normal' in file.stem: tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP); tex.set_editor_property('srgb',False)
    elif 'Occlusion' in file.stem: tex.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_MASKS); tex.set_editor_property('srgb',False)
    save(tex)
u.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
task=u.AssetImportTask(); task.filename=str(root/'ArtSource/LivingThroat/SM_Uvula.fbx'); task.destination_path=folder; task.destination_name='SM_Uvula'; task.automated=True; task.save=True; task.replace_existing=True
opt=u.FbxImportUI(); opt.import_mesh=True; opt.import_as_skeletal=False; opt.import_materials=False; opt.import_textures=False; opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH
opt.static_mesh_import_data.combine_meshes=True; opt.static_mesh_import_data.auto_generate_collision=False
task.options=opt; task.factory=u.FbxFactory(); assets.import_asset_tasks([task]); uvula=lib.load_asset(folder+'/SM_Uvula')
if not lib.does_asset_exist('/Game/Art/Materials/M_LivingTissue'):
    g=Graph('M_LivingTissue',translucent=False)
    g.mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_SUBSURFACE)
    uv=g.node(u.MaterialExpressionTextureCoordinate); uv.set_editor_property('u_tiling',3); uv.set_editor_property('v_tiling',3)
    def texture(suffix,normal=False):
        n=g.node(u.MaterialExpressionTextureSampleParameter2D); n.set_editor_property('parameter_name',suffix)
        n.set_editor_property('texture',lib.load_asset(folder+'/Textures/TissueSwatch_Tissue_'+suffix))
        n.set_editor_property('sampler_type',u.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else u.MaterialSamplerType.SAMPLERTYPE_MASKS if suffix.startswith('Occlusion') else u.MaterialSamplerType.SAMPLERTYPE_COLOR)
        g.link(uv,n,'UVs'); return n
    base=texture('BaseColor'); normal=texture('Normal',True); orm=texture('OcclusionRoughnessMetallic')
    world=g.node(u.MaterialExpressionWorldPosition)
    color=g.custom('Subtle vascular mottling','float n=sin(P.x*.017+sin(P.z*.031))*sin(P.y*.021+P.z*.013); return Base.rgb*Tint.rgb*(1+n*.12);',{'P':world,'Base':base,'Tint':g.vector('TissueTint',(1,1,1,1))},3)
    g.output(color,u.MaterialProperty.MP_BASE_COLOR)
    rough=g.custom('Wet film','return clamp(ORM.g+Bias,.18,.42);',{'ORM':orm,'Bias':g.scalar('RoughnessBias',0)})
    g.output(rough,u.MaterialProperty.MP_ROUGHNESS); g.output(g.scalar('Specular',.48),u.MaterialProperty.MP_SPECULAR)
    g.output(g.custom('Gentle normal','return normalize(float3(N.xy*Strength,N.z));',{'N':normal,'Strength':g.scalar('MicroNormalStrength',.22)},3),u.MaterialProperty.MP_NORMAL)
    g.output(g.custom('Soft red scatter','return Base.rgb*float3(.85,.27,.18);',{'Base':color},3),u.MaterialProperty.MP_SUBSURFACE_COLOR)
    g.output(g.scalar('ScatterDensity',.6),u.MaterialProperty.MP_OPACITY)
    throatmat=g.save('MI_LivingThroat')
    if throatmat is None: throatmat=lib.load_asset('/Game/Art/Materials/MI_LivingThroat')
else:
    g=type('ExistingGraph',(),{'mat':lib.load_asset('/Game/Art/Materials/M_LivingTissue')})()
    throatmat=lib.load_asset('/Game/Art/Materials/MI_LivingThroat')
variants={}
for name,tint,bias in [('MI_MouthGum',(1.25,1.4,1.30,1),0),('MI_MouthCheek',(.95,.8,.83,1),.015),('MI_MouthPalate',(1.10,1.05,.95,1),.02)]:
    path='/Game/Art/Materials/'+name
    mi=lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(name,'/Game/Art/Materials',u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(mi,g.mat); edit.set_material_instance_vector_parameter_value(mi,'TissueTint',u.LinearColor(*tint)); edit.set_material_instance_scalar_parameter_value(mi,'RoughnessBias',bias); edit.update_material_instance(mi); save(mi); variants[name]=mi
ringmat=lib.load_asset('/Game/Art/Materials/MI_ThroatRing')
ringbase=lib.load_asset('/Game/Art/Materials/M_ThroatRing')
if ringbase is None:
    ring=Graph('M_ThroatRing',translucent=True); ring.mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
    ring.output(ring.vector('ZoneColor',(.04,1,.3,1)),u.MaterialProperty.MP_EMISSIVE_COLOR)
    ringmat=ring.save('MI_ThroatRing'); ringbase=ring.mat
if not isinstance(edit.get_material_property_input_node(ringbase,u.MaterialProperty.MP_OPACITY),u.MaterialExpressionVertexColor):
    vc=edit.create_material_expression(ringbase,u.MaterialExpressionVertexColor,-300,300)
    edit.connect_material_property(vc,'A',u.MaterialProperty.MP_OPACITY)
    errors=edit.recompile_material(ringbase)
    if errors: raise RuntimeError(str(errors))
    save(ringbase)
allactors=actors.get_all_level_actors()
old=next((a for a in allactors if isinstance(a,u.MCFoodDisposal) and not a.get_editor_property('brush_bin') and not isinstance(a,u.MCThroat)),None)
throat=next((a for a in allactors if isinstance(a,u.MCThroat)),None)
if throat is None:
    throat=actors.spawn_actor_from_class(u.MCThroat,old.get_actor_location() if old else u.Vector(1300,-30,-40),u.Rotator())
throat.set_actor_label('GAMEPLAY | Living throat'); throat.set_folder_path('Gameplay/Throat')
throat.set_editor_property('tissue_material',throatmat); throat.set_editor_property('ring_material',ringmat)
throat.get_editor_property('uvula').set_static_mesh(uvula)
throat.set_editor_property('uvula_length',190.0)
throat.set_editor_property('uvula_top',u.Vector(-120,0,430))
throat.set_editor_property('anticipation_seconds',3.0)
throat.set_editor_property('gate_center',u.Vector(150,0,-200))
throat.set_editor_property('gate_size',u.Vector2D(500,500))
# Read the actual tongue surface to place the staging zone and the reachable button.
tongue=next(a for a in allactors if isinstance(a,u.MCTongue))
floor=[]
for x in (800,1000,1180,1300):
    hit=u.SystemLibrary.line_trace_single(tongue,u.Vector(x,-30,500),u.Vector(x,-30,-240),u.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[throat],u.DrawDebugTrace.NONE,False)
    floor.append({'x':x,'hit':str(hit)})
for a in allactors:
    if not isinstance(a,u.StaticMeshActor): continue
    label=a.get_actor_label(); comp=a.static_mesh_component
    if label in ('SM_Gum','SM_Gum2'): comp.set_material(0,variants['MI_MouthGum'])
    elif label=='SM_Wall_01': comp.set_material(0,variants['MI_MouthCheek'])
    elif label=='SM_Hole': comp.set_material(0,variants['MI_MouthPalate'])
if old: actors.destroy_actor(old)
throat.get_editor_property('tissue').set_material(0,throatmat)
for slot in range(len(uvula.get_editor_property('static_materials'))):
    uvula.set_material(slot,variants['MI_MouthPalate'])
    throat.get_editor_property('uvula').set_material(slot,variants['MI_MouthPalate'])
throat.get_editor_property('zone_ring').set_material(0,ringmat)
throat.call_method('RebuildAppearance')
save(uvula)
if not levels.save_current_level(): raise RuntimeError('Map save failed')
Path(root/'Saved/LivingThroatIntegration.json').write_text(json.dumps({'actor':throat.get_path_name(),'floor':floor,'uvula_bounds':str(uvula.get_bounding_box())},indent=2),encoding='utf-8')
u.log('MC_LIVING_THROAT_INTEGRATED')
