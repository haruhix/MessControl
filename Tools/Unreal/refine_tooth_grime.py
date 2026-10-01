"""Update only the tooth coating optics; preserve the shared wipe/foam graph."""
import unreal as u

assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before material authoring'
material = u.load_asset('/Game/Gameplay/Care/M_ArenaToothRelief')
nodes = {str(n.get_editor_property('description')): n for n in u.MaterialEditingLibrary.get_material_expressions(material) if isinstance(n,u.MaterialExpressionCustom)}
changes = {
    'MC Care: coatingColor': '''
float3 w=pow(abs(N),4); w/=max(dot(w,1),.001);
float p=Texture2DSample(Tex,TexSampler,P.yz*.027).r*w.x+Texture2DSample(Tex,TexSampler,P.xz*.027).r*w.y+Texture2DSample(Tex,TexSampler,P.xy*.027).r*w.z;
float grain=Texture2DSample(Tex,TexSampler,P.yz*.14+float2(.17,.31)).r*w.x+Texture2DSample(Tex,TexSampler,P.xz*.14+float2(.29,.13)).r*w.y+Texture2DSample(Tex,TexSampler,P.xy*.14+float2(.41,.23)).r*w.z;
grain=saturate(grain*2.8);
float core=smoothstep(.10,.85,F.z)*lerp(.72,1,F.w);
float3 honey=float3(.50,.245,.058);
float3 dense=float3(.245,.093,.018);
float3 color=lerp(honey,dense,core)*lerp(.88,1.12,saturate(p*2.8));
// Pale mineral specks and a thin translucent-looking margin, without holes
// in the gameplay coverage or a screen-space glitter effect.
color=lerp(color,float3(.67,.43,.16),smoothstep(.65,.91,grain)*.22);
color=lerp(float3(.71,.48,.22),color,smoothstep(.25,.86,F.x));
return lerp(color,float3(.92,.97,.94),F.y*.96);
''',
    'MC Care: coatingRoughness': 'return lerp(lerp(.39,.29,F.z)+F.w*.065,.42,F.y);',
    'MC Care: meniscusNormal': '''
float3 n=normalize(N);
float coarse=sin(World.x*.73+sin(World.z*.49))*sin(World.y*.81+World.z*.62);
float fine=sin(World.x*3.7+sin(World.y*1.9)+sin(World.z*4.1))*sin(World.y*4.3+sin(World.x*2.3)+World.z*2.9);
float h=coarse*.025+fine*.008;
float3 dx=ddx(World),dy=ddy(World),rx=cross(dy,n),ry=cross(n,dx);
float det=dot(dx,rx);
return normalize(n-(ddx(h)*rx+ddy(h)*ry)*sign(det)/max(abs(det),1e-5));
''',
    'MC Care: coatingClip': '''
float edge=max(fwidth(F.x)*1.25,.025);
return smoothstep(.25-edge,.25+edge,F.x);
'''
}
for label, code in changes.items():
    nodes[label].set_editor_property('code',code)
function=u.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA')
assert function, 'Engine temporal coverage function is required'
dither=next((n for n in u.MaterialEditingLibrary.get_material_expressions(material) if isinstance(n,u.MaterialExpressionMaterialFunctionCall) and str(n.get_editor_property('desc'))=='MC Care: soft coating edge'),None)
if dither is None:
    dither=u.MaterialEditingLibrary.create_material_expression(material,u.MaterialExpressionMaterialFunctionCall,800,200)
    dither.set_editor_property('desc','MC Care: soft coating edge')
    dither.set_material_function(function)
assert u.MaterialEditingLibrary.connect_material_expressions(nodes['MC Care: coatingClip'],'',dither,'')
assert u.MaterialEditingLibrary.connect_material_property(dither,'',u.MaterialProperty.MP_OPACITY_MASK)
errors=u.MaterialEditingLibrary.recompile_material(material)
assert not errors, str(errors)
assert u.EditorAssetLibrary.save_loaded_asset(material,False)
u.log('MC_GRIME_MATERIAL_SAVED')
