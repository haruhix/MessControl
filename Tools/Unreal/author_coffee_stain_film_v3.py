"""Refine the owned cleanable coffee film, shared by slimes and wave residue.

Idle MessControl only. Default is a read-only plan. Root can apply using runpy
with MC_COFFEE_FILM_APPLY=True after reviewing the plan. No map or other material
is touched. Exact 64x64 cleaning opacity and the native 0.6 cm geometry remain.
"""
import datetime
import hashlib
import json
import runpy
import shutil
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
PATH = '/Game/Cataclysm/Jelly/M_CataclysmPaintTrail'
OUT = ROOT/'Saved/CoffeeStainFilmV3/Authoring.json'
REVISION = 'coffee_stain_film_v3:5'
lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
support = runpy.run_path(str(ROOT/'Tools/Unreal/author_coffee_slime_surfaces_v2.py'), run_name='coffee_film_support')
Graph, reset, require = (support[key] for key in ('Graph', 'reset', 'require'))


def author(mat):
    reset(mat, True)
    g = Graph(mat)
    uv = g.node(u.MaterialExpressionTextureCoordinate)
    pos = g.node(u.MaterialExpressionWorldPosition)
    wipe = g.node(u.MaterialExpressionTextureObjectParameter)
    wipe.set_editor_property('parameter_name', 'WipeMask')
    wipe.set_editor_property('texture', lib.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    wipe.set_editor_property('sampler_type', u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    finish = g.scalar('Finish', 0)
    size = g.scalar('WorldSize', 1200)
    coverage = g.custom('Exact persistent cleanable coffee mask',
        'return Texture2DSampleLevel(Wipe,WipeSampler,UV,0).r*(1-saturate(Finish));',
        dict(UV=uv, Wipe=wipe, Finish=finish))
    g.out(coverage, u.MaterialProperty.MP_OPACITY_MASK)
    tint = g.vector('LiquidColor', (.055, .014, .003))
    crema = g.vector('CoffeeCrema', (.18, .060, .013))
    mottle = g.custom('Quiet fine espresso grain; no streak or camouflage field', '''
struct CoffeeValueNoise
{
    float hash(float2 cell)
    {
        float3 p=frac(float3(cell.x,cell.y,cell.x)*float3(.1031,.11369,.13787));
        p+=dot(p,p.yzx+19.19);
        return frac((p.x+p.y)*p.z);
    }
    float3 value_gradient(float2 coord)
    {
        float2 cell=floor(coord),f=frac(coord);
        float2 fade=f*f*(3-2*f), derivative=6*f*(1-f);
        float a=hash(cell), b=hash(cell+float2(1,0));
        float c=hash(cell+float2(0,1)), d=hash(cell+1);
        return float3(lerp(lerp(a,b,fade.x),lerp(c,d,fade.x),fade.y),
            lerp(b-a,d-c,fade.y)*derivative.x,
            lerp(c-a,d-b,fade.x)*derivative.y);
    }
};
CoffeeValueNoise N;
float2 q=float2(P.x*.8-P.y*.6,P.x*.6+P.y*.8);
float3 fine=N.value_gradient(q*.24+float2(19.7,53.1));
float2 slope=fine.yz;
slope=float2(slope.x*.8+slope.y*.6,-slope.x*.6+slope.y*.8);
return float4(fine.x,0,slope);
''', dict(P=pos), 4)
    color = g.custom('Dark thin espresso film; quiet fine grain and narrow amber contact edge', '''
float edge=1-smoothstep(.27,.45,Coverage);
float bank=smoothstep(.45,.63,Coverage)*(1-smoothstep(.63,.84,Coverage));
float3 pigment=Tint.rgb*(.97+.06*Mottle.x);
return lerp(pigment,Crema.rgb*.36,edge*.18)*(1-.07*bank);
''', {'Tint': (tint, 'RGB'), 'Crema': (crema, 'RGB'), 'Mottle': mottle,
      'Coverage': coverage}, 3)
    g.out(color, u.MaterialProperty.MP_BASE_COLOR)
    depth = g.scalar('MeniscusShadingCm', .35)
    micro_strength = g.scalar('CoffeeMicroNormalStrength', .0025)
    normal = g.custom('Subtle thin-film meniscus; no large raised pigment islands', '''
float e=1.0/64.0;
float x=Texture2DSampleLevel(Wipe,WipeSampler,UV+float2(e,0),0).r-Texture2DSampleLevel(Wipe,WipeSampler,UV-float2(e,0),0).r;
float y=Texture2DSampleLevel(Wipe,WipeSampler,UV+float2(0,e),0).r-Texture2DSampleLevel(Wipe,WipeSampler,UV-float2(0,e),0).r;
float2 meniscus=-float2(x,y)*clamp(Depth,0,1.2)/max(WorldSize*e*2,1);
float2 micro=clamp(Mottle.zw*clamp(MicroStrength,0,.006),-.009,.009)*smoothstep(.40,.85,Coverage);
return normalize(float3(meniscus+micro,1));
''', dict(UV=uv, Wipe=wipe, WorldSize=size, Depth=depth, Mottle=mottle,
          Coverage=coverage, MicroStrength=micro_strength), 3)
    g.out(normal, u.MaterialProperty.MP_NORMAL)
    rough = g.custom('Plain PBR gloss; live-comparable roughness and quiet contact edge',
        'return clamp(Roughness+.006*(Mottle.x-.5)+(1-smoothstep(.27,.45,Coverage))*.020,.035,.20);',
        dict(Mottle=mottle, Coverage=coverage, Roughness=g.scalar('CoffeeRoughness', .085)))
    g.out(rough, u.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Specular', .65), u.MaterialProperty.MP_SPECULAR)
    g.out(g.scalar('Metallic', 0), u.MaterialProperty.MP_METALLIC)
    # The actual rev4 frame already showed a real engine reflection. Remove
    # the fake emissive glints completely instead of adding more art patterns.
    g.out(g.custom('No fake glints, pigment floor or self illumination',
                   'return float3(0,0,0);', {}, 3), u.MaterialProperty.MP_EMISSIVE_COLOR)


def main():
    require(Path(u.Paths.get_project_file_path()).stem == 'MessControl', 'Wrong project')
    require(not u.EditorLevelLibrary.get_pie_worlds(False), 'Preserve active Play')
    require(not list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
            and not list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()), 'Preserve unsaved editor packages')
    mat = lib.load_asset(PATH)
    require(isinstance(mat, u.Material), 'Expected existing material '+PATH)
    require(lib.get_metadata_tag(mat, 'MCCataclysmPaintAuthor') == 'mask_tile_v1', 'Preserve unowned material')
    disk = ROOT/('Content/'+PATH.removeprefix('/Game/')+'.uasset')
    report = dict(status='plan', revision=REVISION, material=PATH,
        before_sha256=hashlib.sha256(disk.read_bytes()).hexdigest(),
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        mask_contract='Exact bilinear WipeMask x (1-Finish), clip .25; no UV warping, WPO or new transparency',
        geometry_contract='Native clipped tongue triangles +0.6cm are unchanged',
        shader_features=['Calm espresso base, only +/-3% fine 4cm pigment variation',
                         '.35cm mask meniscus shading and parameterized fine normal strength .0025',
                         'Plain DefaultLit dielectric response, parameterized roughness .085 and specular .65',
                         'Entire fake reflection/streak/crema field removed; emissive exactly zero'],
        previous_revision_review='v3:1 failed: beige paste/sine bands. v3:2 failed: matte brown fill. Rev3:3 failed: raised beige pudding/camouflage; its report incorrectly counted 2 reflection powers instead of 3. Rev3:4 frame 00176 failed: beige zebra/scratch streaks. The frame already contained a real engine PBR reflection, so all fake glints are removed in rev5.',
        shader_cost_note='Five mask texture reads; one four-corner value-noise evaluation (4 hashes), ZERO custom reflection powers, no derivative AA or new textures/draws/transparency layers. GPU cost is unmeasured.',
        texture_reads=5, gameplay_visual_review='pending', gpu_cost='not measured',
        references=['https://realtimevfx.com/t/vfx-experiments-personal-wip-sam-perzanowski/1278',
                    'https://dev.epicgames.com/documentation/en-us/unreal-engine/material-inputs-in-unreal-engine',
                    'https://axton.artstation.com/projects/nYeAJe',
                    'https://alexanderzawadski.artstation.com/blog/boaaN/unreal-water-material-breakdown'])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    if OUT.exists():
        previous = OUT.read_bytes()
        history = OUT.parent/'History'/(hashlib.sha256(previous).hexdigest()+'.json')
        history.parent.mkdir(parents=True, exist_ok=True)
        if not history.exists():
            history.write_bytes(previous)
        report['previous_report_backup'] = str(history)
    if globals().get('MC_COFFEE_FILM_APPLY', False):
        backup = OUT.parent/'Backup'/report['before_sha256']/disk.name
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(disk, backup)
        report['backup'] = str(backup)
        OUT.write_text(json.dumps(report, indent=2), encoding='utf-8')
        author(mat)
        errors = edit.recompile_material(mat)
        require(not errors, 'Material compile errors: '+str(errors))
        lib.set_metadata_tag(mat, 'MCCoffeeSurfaceRevision', REVISION)
        require(lib.save_loaded_asset(mat, False), 'Could not save owned material')
        report.update(status='authored', compiled=True, nodes=edit.get_num_material_expressions(mat),
            after_sha256=hashlib.sha256(disk.read_bytes()).hexdigest())
    report['utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    OUT.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
