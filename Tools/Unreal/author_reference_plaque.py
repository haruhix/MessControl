"""Apply the supplied yellow/brown plaque reference to the arena-only coating."""
import unreal as u,json
from pathlib import Path

root=Path(u.Paths.project_dir());out=root/'Artifacts/GrimeReference_20261017'
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
path='/Game/Gameplay/Care/M_ArenaToothRelief'
assert path not in {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
e=u.MaterialEditingLibrary;lib=u.EditorAssetLibrary
texture_path='/Game/Gameplay/Care/Textures/T_PlaqueReference'
if not lib.does_asset_exist(texture_path):
 task=u.AssetImportTask();task.set_editor_properties(dict(filename=str(root/'SourceArt/Care/PlaqueReference.png'),destination_path='/Game/Gameplay/Care/Textures',destination_name='T_PlaqueReference',automated=True,save=True,replace_existing=False))
 u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture=lib.load_asset(texture_path);assert texture
texture.set_editor_property('srgb',True)
texture.set_editor_property('address_x',u.TextureAddress.TA_WRAP);texture.set_editor_property('address_y',u.TextureAddress.TA_WRAP)
lib.save_loaded_asset(texture,False)
m=lib.load_asset(path);nodes=list(e.get_material_expressions(m))
cs={str(n.get_editor_property('description')):n for n in nodes if isinstance(n,u.MaterialExpressionCustom)}
ps={str(n.get_editor_property('parameter_name')):n for n in nodes if isinstance(n,(u.MaterialExpressionScalarParameter,u.MaterialExpressionVectorParameter,u.MaterialExpressionTextureObjectParameter))}
with u.ScopedEditorTransaction('Reference plaque with fixed centimetre pigment scale'):
 ps['GrimePigment'].set_editor_property('texture',texture)
 for name,value in [('GrimeDetailSize',180.),('GrimeMicroRelief',.13),('GrimeNormalStrength',.12)]:ps[name].set_editor_property('default_value',value)
 cs['MC Care: coatingColor'].set_editor_property('code','''
// The supplied ochre wash and brown specks retain their own size in cm.
float3 color=lerp(Pigment,float3(.79,.48,.14),.10);
float margin=1-smoothstep(.23,.70,F.x);
color=lerp(color,float3(.90,.73,.37),margin*.20);
return lerp(color,float3(.92,.97,.94),F.y*.92);
''')
 cs['MC Care: coatingRoughness'].set_editor_property('code','return lerp(lerp(.64,.78,D.z),.43,F.y);')
 cs['MC Care: meniscusNormal'].set_editor_property('code','''
float3 n=normalize(N);
float h=saturate(F.x)*(.06+.08*F.z+D.w*max(Strength,0));
float3 dx=ddx(World),dy=ddy(World),rx=cross(dy,n),ry=cross(n,dx);
float det=dot(dx,rx);
float sx=ddx(h)+saturate(F.x)*max(NormalStrength,0)*dot(Slope,ddx(P*Scale));
float sy=ddy(h)+saturate(F.x)*max(NormalStrength,0)*dot(Slope,ddy(P*Scale));
float3 gradient=(sx*rx+sy*ry)*sign(det)/max(abs(det),1e-5);
return normalize(n-clamp(gradient,-.25,.25));
''')
 errors=list(e.recompile_material(m));assert not errors,errors
 assert lib.save_loaded_asset(m,False)
(out/'Material.json').write_text(json.dumps(dict(material=path,reference_texture=texture_path,detail_size_cm=180,compile_errors=errors),indent=2))
print('REFERENCE_PLAQUE_SAVED',errors)
