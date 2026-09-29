"""Painter tissue, wet enamel and amber deposits. Run in the editor after earlier art scripts."""
import unreal as u
from pathlib import Path
root=Path(u.Paths.project_dir()).resolve(); lib=u.EditorAssetLibrary; edit=u.MaterialEditingLibrary
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
assert not u.EditorLevelLibrary.get_pie_worlds(True), 'Stop PIE before material authoring'
def save(a):
    assert lib.save_loaded_asset(a,False), a.get_path_name()
def code(mat,label,value):
    n=next(n for n in edit.get_material_expressions(mat) if isinstance(n,u.MaterialExpressionCustom) and str(n.get_editor_property('description'))==label)
    n.set_editor_property('code',value)
def scalar(mi,name,value):edit.set_material_instance_scalar_parameter_value(mi,name,value)
def vector(mi,name,value):edit.set_material_instance_vector_parameter_value(mi,name,u.LinearColor(*value))

for suffix in ('BaseColor','Normal','OcclusionRoughnessMetallic'):
    name='TissueSwatch_Tissue_'+suffix
    task=u.AssetImportTask(); task.filename=str(root/'ArtSource/LivingThroat/Textures'/(name+'.png'))
    task.destination_path='/Game/Gameplay/Throat/Textures'; task.destination_name=name
    task.automated=True; task.replace_existing=True; task.save=True
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    t=lib.load_asset(task.destination_path+'/'+name)
    t.set_editor_property('srgb',suffix=='BaseColor')
    t.set_editor_property('compression_settings',u.TextureCompressionSettings.TC_NORMALMAP if suffix=='Normal' else u.TextureCompressionSettings.TC_MASKS if suffix!='BaseColor' else u.TextureCompressionSettings.TC_DEFAULT)
    save(t)

for name in ('M_LivingTissue','M_ThroatSculpt'):
    m=lib.load_asset('/Game/Art/Materials/'+name)
    code(m,'Wet film','return clamp(.11+ORM.g*.43+Bias,.13,.34);')
    code(m,'Subtle vascular mottling','''
float n=sin(P.x*.011+sin(P.y*.019))*sin(P.z*.016+P.y*.008);
float m=sin(P.x*.038-P.z*.027+sin(P.y*.036));
float bloom=saturate(.5+.38*n+.12*m);
return Base.rgb*Tint.rgb*lerp(float3(.67,.50,.64),float3(1.20,1.33,1.24),bloom);
''')
    code(m,'Soft red scatter','return Base.rgb*float3(.8,.30,.40);')
    if name=='M_ThroatSculpt':
        code(m,'Throat depth','return C*(1.0-.96*smoothstep(120.0,800.0,P.x));')
    assert not edit.recompile_material(m); save(m)
for name,tint,normal,bias in [
    ('MI_MouthGum',(1.38,3.0,2.7,1),.33,-.020),
    ('MI_MouthCheek',(1.0,1.35,1.3,1),.36,.018),
    ('MI_MouthPalate',(1.10,1.70,1.55,1),.30,.005),
    ('MI_LivingThroat',(1.25,1.45,1.65,1),.36,-.005)]:
    m=lib.load_asset('/Game/Art/Materials/'+name)
    vector(m,'TissueTint',tint); scalar(m,'MicroNormalStrength',normal)
    scalar(m,'RoughnessBias',bias); scalar(m,'Specular',.55); scalar(m,'ScatterDensity',.83)
    edit.update_material_instance(m); save(m)

# Keep all contact/wipe inputs; only the optical response changes.
m=lib.load_asset('/Game/Gameplay/Care/M_ArenaToothCare')
code(m,'MC Care: cleaningColor','''
float3 cream=float3(.91,.79,.62)*lerp(.96,1.04,saturate(Enamel.r));
float3 pigment=Tint*lerp(.7,1.5,saturate(Pigment.r*2.5));
float3 color=lerp(cream,pigment,F.x);
color=lerp(color,float3(.075,.024,.012),F.w*.85);
color=lerp(color,float3(.91,.98,.95),F.y*.95);
return lerp(color,float3(1,.3,.08),saturate(Flash)*.55);
''')
code(m,'MC Care: cleaningRoughness','return lerp(lerp(.18,.135,F.z*.6),.38,F.y);')
assert not edit.recompile_material(m);save(m)
m=lib.load_asset('/Game/Gameplay/Care/M_ArenaToothRelief')
code(m,'MC Care: coatingColor','''
float3 w=pow(abs(N),4); w/=max(dot(w,1),.001);
float p=Texture2DSample(Tex,TexSampler,P.yz*.027).r*w.x+Texture2DSample(Tex,TexSampler,P.xz*.027).r*w.y+Texture2DSample(Tex,TexSampler,P.xy*.027).r*w.z;
float3 amber=float3(.43,.14,.018);
float3 dark=float3(.14,.032,.003);
float core=smoothstep(.12,.88,F.z)*lerp(.68,1,F.w);
float3 color=lerp(amber,dark,core)*lerp(.78,1.30,saturate(p*3));
color=lerp(color,float3(.72,.36,.065),smoothstep(.77,.94,p)*.32);
return lerp(color,float3(.92,.97,.94),F.y*.96);
''')
code(m,'MC Care: coatingRoughness','return lerp(lerp(.22,.105,F.z)+F.w*.045,.38,F.y);')
code(m,'MC Care: meniscusNormal','''
float3 n=normalize(N);
// Meshed rounded clumps supply the broad normals; add only fine surface grain.
float h=(sin(World.x*.73+sin(World.z*.49))*sin(World.y*.81+World.z*.62))*.035;
float3 dx=ddx(World),dy=ddy(World),rx=cross(dy,n),ry=cross(n,dx);
float det=dot(dx,rx);
return normalize(n-(ddx(h)*rx+ddy(h)*ry)*sign(det)/max(abs(det),1e-5));
''')
assert not edit.recompile_material(m);save(m)

for path in ('/Game/Gameplay/Arena/MI_TonguePain','/Game/Gameplay/Arena/Pressure/MI_Pressure_Soft','/Game/Gameplay/Arena/Pressure/MI_Pressure_Deep'):
    mi=lib.load_asset(path)
    vector(mi,'Albedo Color',(.68,.63,.82,1)); scalar(mi,'Spec',.52)
    scalar(mi,'Normal Strength',.42);scalar(mi,'Roughness Strength',.95)
    edit.update_material_instance(mi);save(mi)

for a in actors.get_all_level_actors():
    label=a.get_actor_label()
    if isinstance(a,u.SkyLight):a.get_component_by_class(u.SkyLightComponent).set_intensity(.24)
    elif isinstance(a,u.DirectionalLight):
        c=a.get_component_by_class(u.DirectionalLightComponent);c.set_intensity(1.5)
        c.set_light_color(u.LinearColor(1,.97,.93,1));c.set_editor_property('specular_scale',.5)
    if label.startswith('LOOK | Enamel '):
        c=a.get_component_by_class(u.RectLightComponent);c.set_intensity(110)
        c.set_source_width(180);c.set_source_height(320);c.set_light_color(u.LinearColor(1,.97,.92,1))
    if label=='LOOK | Soft key':
        c=a.get_component_by_class(u.RectLightComponent);c.set_intensity(120)
        c.set_source_width(280);c.set_source_height(420);c.set_light_color(u.LinearColor(1,.96,.9,1))
    if label=='LOOK | Soft fill':a.get_component_by_class(u.RectLightComponent).set_intensity(28)
    if label=='LOOK | Mucosa key':
        c=a.get_component_by_class(u.RectLightComponent);c.set_intensity(105)
        c.set_source_width(150);c.set_source_height(420);c.set_light_color(u.LinearColor(1,.97,.94,1))
        a.set_actor_location(u.Vector(150,-520,340),False,True)
        a.set_actor_rotation(u.MathLibrary.find_look_at_rotation(a.get_actor_location(),u.Vector(1650,100,80)),False)
    if isinstance(a,u.PostProcessVolume) and label=='LOOK | Coral Reference':
        s=a.settings
        for k,v in dict(auto_exposure_bias=.15,color_contrast=u.Vector4(1,1,1,1.055),color_saturation=u.Vector4(1,1,1,1.03),ambient_occlusion_intensity=.65,ambient_occlusion_radius=90.0,bloom_intensity=.12).items():
            s.set_editor_property(k,v);s.set_editor_property('override_'+k,True)
        a.set_editor_property('settings',s)
    if isinstance(a,u.MCArenaToothSocket):a.get_editor_property('preview').set_hidden_in_game(True)
# A broad front reflection is readable on curved enamel from the gameplay camera.
front=next((a for a in actors.get_all_level_actors() if a.get_actor_label()=='LOOK | Enamel softbox'),None)
if front is None:
    front=actors.spawn_actor_from_class(u.RectLight,u.Vector())
    front.set_actor_label('LOOK | Enamel softbox');front.set_folder_path('Look/ReferenceLighting')
front.set_actor_location(u.Vector(-850,0,350),False,True)
front.set_actor_rotation(u.MathLibrary.find_look_at_rotation(front.get_actor_location(),u.Vector(300,0,60)),False)
c=front.get_component_by_class(u.RectLightComponent);c.set_mobility(u.ComponentMobility.MOVABLE)
c.set_intensity_units(u.LightUnits.CANDELAS);c.set_intensity(85)
c.set_source_width(950);c.set_source_height(300);c.set_attenuation_radius(2300)
c.set_light_color(u.LinearColor(1,.97,.92,1));c.set_cast_shadows(False)
u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
u.log('MC_RICH_MOUTH_FINISHED')
