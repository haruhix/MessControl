"""Author the compact, neutral wind vortex materials in native Unreal.

Run through UnrealEditor-Cmd -run=pythonscript -script=<this absolute path>.
Creates three materials once and preserves subsequent artist edits. Set
MC_REBUILD_THROAT_VORTEX=1 only to deliberately re-author those materials.
The runtime actor shares three mesh sections per meal; it spawns no emitters
or particle actors per food portion and never touches food collision packages.
"""
import json
import os
from pathlib import Path
import unreal as u

FOLDER = '/Game/Gameplay/VFX/Throat'
lib = u.EditorAssetLibrary
edit = u.MaterialEditingLibrary
assets = u.AssetToolsHelpers.get_asset_tools()
reports = []


def node(mat, cls, x, y, **properties):
    expression = edit.create_material_expression(mat, cls, x, y)
    expression.set_editor_properties(properties)
    return expression


def custom(mat, description, code, inputs, x, y, output):
    expression = node(mat, u.MaterialExpressionCustom, x, y,
                      description=description, code=code, output_type=output)
    entries = []
    for key in inputs:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', key)
        entries.append(entry)
    expression.set_editor_property('inputs', entries)
    for key, (source, pin) in inputs.items():
        assert edit.connect_material_expressions(source, pin, expression, key), key
    return expression


def build(name, code, depth_fade=10):
    path = FOLDER + '/' + name
    exists = lib.does_asset_exist(path)
    if exists and os.environ.get('MC_REBUILD_THROAT_VORTEX') != '1':
        mat = lib.load_asset(path)
        assert isinstance(mat, u.Material), path
        reports.append(dict(path=mat.get_path_name(), preserved=True))
        return
    mat = lib.load_asset(path) if exists else assets.create_asset(name, FOLDER, u.Material, u.MaterialFactoryNew())
    assert isinstance(mat, u.Material), path
    edit.delete_all_material_expressions(mat)
    mat.set_editor_properties(dict(
        blend_mode=u.BlendMode.BLEND_TRANSLUCENT,
        shading_model=u.MaterialShadingModel.MSM_UNLIT,
        two_sided=True,
        disable_depth_test=False,
    ))
    uv = node(mat, u.MaterialExpressionTextureCoordinate, -1100, 0)
    color = node(mat, u.MaterialExpressionVertexColor, -1100, 180)
    age = node(mat, u.MaterialExpressionScalarParameter, -1100, 380,
               parameter_name='VortexAge', default_value=0, group='Intake Vortex')
    opacity = node(mat, u.MaterialExpressionScalarParameter, -1100, 520,
                   parameter_name='Opacity', default_value=1, group='Intake Vortex')
    field = custom(mat, 'Soft turbulent wind with irregular feathered edges', code,
                   {'UV': (uv, ''), 'Age': (age, '')}, -550, 0,
                   u.CustomMaterialOutputType.CMOT_FLOAT2)
    emission = custom(mat, 'Neutral wind shading without additive glow',
                      'return Tint.rgb * float3(.90,.95,1.0) * F.y;', {'Tint': (color, ''), 'F': (field, '')},
                      -200, -100, u.CustomMaterialOutputType.CMOT_FLOAT3)
    alpha = custom(mat, 'Section, particle, and synchronized meal envelope',
                   'return saturate(F.x * Alpha * Envelope);',
                   {'F': (field, ''), 'Alpha': (color, 'A'), 'Envelope': (opacity, '')},
                   -200, 150, u.CustomMaterialOutputType.CMOT_FLOAT1)
    fade = node(mat, u.MaterialExpressionDepthFade, 120, 150,
                fade_distance_default=depth_fade)
    assert edit.connect_material_expressions(alpha, '', fade, 'Opacity')
    assert edit.connect_material_property(emission, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    assert edit.connect_material_property(fade, '', u.MaterialProperty.MP_OPACITY)
    errors = edit.recompile_material(mat)
    assert not errors, (path, errors)
    assert lib.save_loaded_asset(mat, only_if_is_dirty=False), path
    reports.append(dict(path=mat.get_path_name(), preserved=False, saved=True,
                        blend='translucent',
                        shader_compile_errors=errors or [], depth_fade=depth_fade))


# Small analytic fields need no textures, screen-color/refraction sampling,
# scene lights, volumetric solver, particle collision, or extra emitter draws.
build('M_ThroatVortexRibbon', r'''
struct WindNoise {
    float hash(float2 p) {
        float3 q=frac(float3(p.x,p.y,p.x)*.1031);
        q+=dot(q,q.yzx+33.33);
        return frac((q.x+q.y)*q.z);
    }
    float value(float2 p) {
        float2 i=floor(p), f=frac(p);
        f=f*f*(3.0-2.0*f);
        return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),
                    lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
    }
    float fbm(float2 p) {
        float n=0, weight=.5;
        [unroll] for(int k=0;k<3;k++) {
            n+=value(p)*weight;
            p=float2(p.x*1.70-p.y*1.10,p.x*1.10+p.y*1.70)+float2(3.1,7.2);
            weight*=.5;
        }
        return n*1.142857;
    }
}; WindNoise N;
float x=UV.x, y=UV.y*2.0-1.0;
float2 flow=float2(x*11.0-Age*3.3,y*2.1+Age*.13);
float warp=(N.value(flow*.43+float2(7.1,3.4))-.5)*.58;
float fine=N.value(flow*float2(2.9,3.8)+float2(21.7,9.1));
float body=N.fbm(flow+float2(warp*.7,warp*.3));
float edge=1.0-smoothstep(.10+fine*.16,.73+fine*.33,abs(y+warp));
float broken=smoothstep(.32,.72,body);
float threads=.34+.66*smoothstep(.19,.82,fine);
float taper=smoothstep(.02,.13,x)*(1.0-smoothstep(.78,.99,x));
return float2(saturate(edge*broken*threads*taper*.62),.30+body*.055);
''', depth_fade=18)

build('M_ThroatVortexMist', r'''
struct WindNoise {
    float hash(float2 p) {
        float3 q=frac(float3(p.x,p.y,p.x)*.1031);
        q+=dot(q,q.yzx+33.33);
        return frac((q.x+q.y)*q.z);
    }
    float value(float2 p) {
        float2 i=floor(p), f=frac(p);
        f=f*f*(3.0-2.0*f);
        return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),
                    lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
    }
    float fbm(float2 p) {
        float n=0, weight=.5;
        [unroll] for(int k=0;k<3;k++) {
            n+=value(p)*weight;
            p=float2(p.x*1.70-p.y*1.10,p.x*1.10+p.y*1.70)+float2(3.1,7.2);
            weight*=.5;
        }
        return n*1.142857;
    }
}; WindNoise N;
float2 p=(UV-.5)*2.0;
float2 flow=p*2.5+float2(-Age*.72,Age*.21);
float2 warp=float2(N.value(flow*.51+float2(11.3,4.7)),
                   N.value(flow*.51+float2(3.8,17.1)))-.5;
float density=N.fbm(flow+warp*.9);
float fine=N.value(flow*2.7+float2(31.6,12.3));
float edge=1.0-smoothstep(.19,1.06,length(p+warp*.40)+(fine-.5)*.17);
float pockets=smoothstep(.24,.75,density);
return float2(edge*edge*pockets*(.35+fine*.35),.28+density*.06);
''', depth_fade=24)

# The historical Glow asset name is retained for references. It now contains
# ordinary translucent inner wisps and dust, without a luminous core or rings.
build('M_ThroatVortexGlow', r'''
struct WindNoise {
    float hash(float2 p) {
        float3 q=frac(float3(p.x,p.y,p.x)*.1031);
        q+=dot(q,q.yzx+33.33);
        return frac((q.x+q.y)*q.z);
    }
    float value(float2 p) {
        float2 i=floor(p), f=frac(p);
        f=f*f*(3.0-2.0*f);
        return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),
                    lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
    }
    float fbm(float2 p) {
        float n=0, weight=.5;
        [unroll] for(int k=0;k<3;k++) {
            n+=value(p)*weight;
            p=float2(p.x*1.70-p.y*1.10,p.x*1.10+p.y*1.70)+float2(3.1,7.2);
            weight*=.5;
        }
        return n*1.142857;
    }
}; WindNoise N;
float2 p=(UV-.5)*2.0;
float2 flow=float2(UV.x*8.2-Age*2.6,p.y*3.4);
float density=N.fbm(flow+float2(19.3,5.1));
float fine=N.value(flow*2.4+float2(4.9,28.7));
float feather=1.0-smoothstep(.15,.78+fine*.20,abs(p.y+(density-.5)*.32));
float radial=exp(-dot(p,p)*1.7);
float pockets=smoothstep(.27,.72,density);
return float2(radial*feather*pockets*(.32+fine*.34),.31);
''', depth_fade=16)

report = dict(materials=reports, max_food_wakes=24, shared_sections=3,
              collision=False, per_piece_vfx_actors=0,
              palette='neutral cool gray; all three sections translucent, restrained self illumination',
              shader='sin-free hash, smooth value noise, 3 octave fBm, advected domain warp and irregular feathering',
              recipe='6 irregular inward moving partial broad arcs in different planes, '
                     '5 inner wisps, 24 turbulent internal billows, 20 muted dust flecks, '
                     '3 faint delivery gusts, 8 drifting mist puffs, up to 24 soft food wakes; '
                     'compact volume collapses into the opening over the final 0.4 seconds')
Path(u.Paths.project_saved_dir(), 'ThroatVortexMaterials.json').write_text(
    json.dumps(report, indent=2), encoding='utf-8')
u.log('MC_THROAT_VORTEX_MATERIALS_PASS ' + json.dumps(report))
