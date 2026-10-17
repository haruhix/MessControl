"""Larger randomized plaque pigment and a softly eroded coating boundary."""
import unreal as u,json
from pathlib import Path

root=Path(u.Paths.project_dir());out=root/'Artifacts/GrimeNatural_20261017';out.mkdir(parents=True,exist_ok=True)
assert not u.get_editor_subsystem(u.LevelEditorSubsystem).is_in_play_in_editor()
path='/Game/Gameplay/Care/M_ArenaToothRelief'
assert path not in {p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
e=u.MaterialEditingLibrary;m=u.load_asset(path);nodes=list(e.get_material_expressions(m))
cs={str(n.get_editor_property('description')):n for n in nodes if isinstance(n,u.MaterialExpressionCustom)}
ps={str(n.get_editor_property('parameter_name')):n for n in nodes if isinstance(n,u.MaterialExpressionScalarParameter)}

HELPERS=r'''
struct PlaqueNoise {
 float hash(float3 p) { return frac(sin(dot(p,float3(127.1,311.7,74.7)))*43758.5453); }
 float noise(float3 p) {
  float3 i=floor(p),f=frac(p);f=f*f*(3-2*f);
  return lerp(lerp(lerp(hash(i),hash(i+float3(1,0,0)),f.x),
                   lerp(hash(i+float3(0,1,0)),hash(i+float3(1,1,0)),f.x),f.y),
              lerp(lerp(hash(i+float3(0,0,1)),hash(i+float3(1,0,1)),f.x),
                   lerp(hash(i+float3(0,1,1)),hash(i+float3(1,1,1)),f.x),f.y),f.z);
 }
 float2 rotate(float2 p,float a) {
  float s=sin(a),c=cos(a);return float2(c*p.x-s*p.y,s*p.x+c*p.y);
 }
 float3 stochastic(Texture2D tex,SamplerState samp,float2 uv,float seed) {
  // Three neighboring random patches form one continuous triangular blend.
  float2 skew=float2(uv.x-.577350269*uv.y,1.154700538*uv.y)*1.6;
  float2 cell=floor(skew),f=frac(skew),a,b,c;float3 weights;
  if(f.x+f.y<=1) {
   a=cell;b=cell+float2(1,0);c=cell+float2(0,1);
   weights=float3(1-f.x-f.y,f.x,f.y);
  } else {
   a=cell+1;b=cell+float2(0,1);c=cell+float2(1,0);
   weights=float3(f.x+f.y-1,1-f.x,1-f.y);
  }
  weights=weights*weights*weights;weights/=max(dot(weights,1),.0001);
  float2 dx=ddx(uv),dy=ddy(uv);float3 color=0;
  [unroll] for(int j=0;j<3;j++) {
   float2 id=j==0?a:j==1?b:c;
   float3 h=float3(hash(float3(id,seed)),hash(float3(id+17.31,seed+3.7)),hash(float3(id+53.13,seed+11.9)));
   float angle=h.z*6.2831853,scale=lerp(.86,1.10,h.x);
   float2 q=rotate(uv,angle)*scale+h.xy*7.17;
   color+=Texture2DSampleGrad(tex,samp,q,rotate(dx,angle)*scale,rotate(dy,angle)*scale).rgb*weights[j];
  }
  return color;
 }
};
PlaqueNoise H;
'''

pigment=cs['MC Care: anchored plaque detail pigment']
clip=cs['MC Care: coatingClip']
old_codes={label:cs[label].get_editor_property('code') for label in ['MC Care: anchored plaque detail pigment','MC Care: coatingClip']}
with u.ScopedEditorTransaction('Larger stochastic plaque with feathered natural margins'):
 size=ps.get('GrimePigmentSize')
 if size is None:
  size=e.create_material_expression(m,u.MaterialExpressionScalarParameter,1000,5100)
  size.set_editor_property('parameter_name','GrimePigmentSize');size.set_editor_property('group','Grime optics')
 size.set_editor_property('default_value',420.)
 assert e.connect_material_expressions(size,'',pigment,'Tile')
 pigment.set_editor_property('code',HELPERS+r'''
 [branch] if(Amount<=.0001) return float3(.82,.66,.36);
 float3 cm=P*Scale;
 float3 q=cm/max(Tile,8.0);
 float3 warp=float3(H.noise(cm/135+Seed*2.31),H.noise(cm/135+19.73+Seed*3.17),H.noise(cm/135+41.19+Seed*1.37));
 q+=(warp-.5)*.10;
 float3 w=pow(abs(normalize(N)),4);w/=max(dot(w,1),.0001);
 float3 color=0;
 color+=H.stochastic(Pigment,PigmentSampler,q.yz,Seed+7.1)*w.x;
 color+=H.stochastic(Pigment,PigmentSampler,q.xz,Seed+31.7)*w.y;
 color+=H.stochastic(Pigment,PigmentSampler,q.xy,Seed+53.3)*w.z;
 return color;
''')
 # The procedural film's UVs describe its broad coverage window, independent
 # of crown UV seams. Pixel erosion softens this window without changing care.
 uv=next((n for n in nodes if isinstance(n,u.MaterialExpressionTextureCoordinate) and str(n.get_editor_property('desc'))=='Natural plaque window UV'),None)
 if uv is None:
  uv=e.create_material_expression(m,u.MaterialExpressionTextureCoordinate,1000,5350)
  uv.set_editor_property('desc','Natural plaque window UV')
 entries=list(clip.get_editor_property('inputs'));names={str(i.get_editor_property('input_name')) for i in entries}
 for name in ['UV','P','Scale','Seed','Pigment']:
  if name not in names:
   item=u.CustomInput();item.set_editor_property('input_name',name);entries.append(item)
 clip.set_editor_property('inputs',entries)
 for name,src in [('UV',uv),('Seed',ps['GrimeSeed']),('Pigment',pigment)]:assert e.connect_material_expressions(src,'',clip,name)
 # Reuse the existing rest position and size-correct scale nodes.
 # The named graph inputs are available as expression inputs through the library.
 pos=next(n for n in nodes if isinstance(n,u.MaterialExpressionVertexInterpolator) and any(isinstance(s,u.MaterialExpressionPreSkinnedPosition) for s in e.get_inputs_for_material_expression(m,n)))
 scale=next(n for n in nodes if isinstance(n,u.MaterialExpressionVectorParameter) and str(n.get_editor_property('parameter_name'))=='GrimeScale')
 assert e.connect_material_expressions(pos,'',clip,'P');assert e.connect_material_expressions(scale,'',clip,'Scale')
 clip.set_editor_property('code',HELPERS+r'''
 float3 cm=P*Scale;
 float broad=H.noise(cm/58+Seed*float3(2.7,1.3,4.1));
 float medium=H.noise(cm/19+Seed*float3(5.1,3.7,2.3));
 float fine=H.noise(cm/5.5+Seed*2.13);
 float2 q=UV*2-1;
 float boundary=1-max(abs(q.x),abs(q.y));
 float ragged=(broad-.5)*.29+(medium-.5)*.11+(fine-.5)*.035;
 float feather=smoothstep(.025,.29,boundary+ragged);
 // A translucent-looking wash joins the enamel rather than a clipped sticker.
 float paper=lerp(.62,1,smoothstep(.10,.58,1-Pigment.g));
 float wipe=smoothstep(.06,.66,F.x);
 return saturate(feather*paper*wipe);
''')
 m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
 m.set_editor_property('translucency_lighting_mode',u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
 assert e.connect_material_property(clip,'',u.MaterialProperty.MP_OPACITY)
 errors=list(e.recompile_material(m))
 if errors:
  for label,code in old_codes.items():cs[label].set_editor_property('code',code)
  e.recompile_material(m)
  raise RuntimeError(errors)
 assert u.EditorAssetLibrary.save_loaded_asset(m,False)
stats=e.get_statistics(m)
report=dict(material=path,compile_errors=errors,pigment_size_cm=420,previous_pigment_size_cm=180,scale_increase=420/180,stochastic='Three random rotations/offsets/scales per triangular cell, per tooth seed, three rest-space projections, restrained spatial warp',edge='Seeded broad/medium/fine erosion and continuously blended wash; lit surface translucency',statistics={n:getattr(stats,n) for n in ['num_pixel_shader_instructions','num_samplers','num_pixel_texture_samples']})
(out/'Material.json').write_text(json.dumps(report,indent=2))
print('NATURAL_PLAQUE_SAVED',report)
