"""Native temporal feather on accepted rev5; replace rejected cardinal edges.

Idle/clean MessControl only. Plan by default; root applies with
MC_COFFEE_EDGE_APPLY=True. Remove only the two owned cardinal nodes and restore
original UV. Feather a narrow .25 coverage band through Engine DitherTemporalAA
in this masked material. Native mask, Finish, appearance and geometry remain;
zero coverage stays invisible and the core opaque. Requires live TAA review.
"""
import ast
import datetime
import hashlib
import json
import runpy
import shutil
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
PATH = '/Game/Cataclysm/Jelly/M_CataclysmPaintTrail'
REVISION = 'coffee_edge_reconstruction_v1:2'
TAG, OWNER = 'MCCoffeeEdgeAuthor', 'temporal_coverage_v2'
OUT = ROOT/'Saved/CoffeeStainEdges/Authoring.json'
appearance_source = ROOT/'Tools/Unreal/author_coffee_stain_film_v3.py'
support = runpy.run_path(str(appearance_source), run_name='coffee_edges_support')
Graph, require = support['Graph'], support['require']
lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
OLD_DESCRIPTION = 'Cardinal cubic-Hermite coffee mask reconstruction'
OLD_CODE = '''
float2 texel=UV*64.0-.5;
float2 cell=floor(texel), fraction=frac(texel);
float2 cubic=fraction*fraction*(3.0-2.0*fraction);
float2 weight=lerp(fraction,cubic,saturate(Strength));
return (cell+weight+.5)/64.0;
'''
FUNCTION = '/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'
FEATHER_DESCRIPTION = 'Narrow world-bounded coffee temporal coverage'
DITHER_DESCRIPTION = 'Coffee edge native DitherTemporalAA'
COMBINE_DESCRIPTION = 'Coffee edge preserve zero mask and opaque core'
FEATHER_CODE = '''
float3 dx=ddx(P),dy=ddy(P),n=cross(dx,dy);
n*=rsqrt(max(dot(n,n),1.e-12));
float3 rx=cross(dy,n),ry=cross(n,dx);
float determinant=dot(dx,rx);
float3 gradient=(ddx(Coverage)*rx+ddy(Coverage)*ry)
    *sign(determinant)/max(abs(determinant),1.e-8);
float pixel_cm=max(length(dx),length(dy));
float full_width_cm=clamp(pixel_cm*1.5,2.,4.);
float half_alpha=clamp(length(gradient)*full_width_cm*.5,.001,.24);
float alpha=smoothstep(.25-half_alpha,.25+half_alpha,Coverage);
return Coverage>0 ? alpha : 0;
'''
COMBINE_CODE = '''
float temporal=Coverage>=.5 ? 1. : Temporal;
float result=lerp(Coverage,temporal,saturate(Strength));
return Coverage>0 ? result : 0;
'''


def source_code(description):
    for node in ast.walk(ast.parse(appearance_source.read_text(encoding='utf-8-sig'))):
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == 'custom' \
                and len(node.args) >= 2 and isinstance(node.args[0], ast.Constant) and node.args[0].value == description:
            return ast.literal_eval(node.args[1])
    raise RuntimeError('Accepted appearance block missing: '+description)


def main():
    require(Path(u.Paths.get_project_file_path()).stem == 'MessControl', 'Wrong project')
    require(not u.EditorLevelLibrary.get_pie_worlds(False), 'Preserve active PIE')
    require(not list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
            and not list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages()), 'Preserve unsaved packages')
    material = lib.load_asset(PATH)
    require(isinstance(material, u.Material) and lib.get_metadata_tag(material, 'MCCataclysmPaintAuthor') == 'mask_tile_v1',
            'Preserve unowned material')
    require(support['REVISION'] == 'coffee_stain_film_v3:5'
            and lib.get_metadata_tag(material, 'MCCoffeeSurfaceRevision') == 'coffee_stain_film_v3:5',
            'Apply/review the accepted plain-PBR rev5 before the scoped edge patch')
    require(material.get_editor_property('blend_mode') == u.BlendMode.BLEND_MASKED
            and abs(float(material.get_editor_property('opacity_mask_clip_value'))-.25) < 1.e-6,
            'Preserve the existing masked clip contract')
    nodes = list(edit.get_material_expressions(material))
    customs = {str(n.get_editor_property('description')): n for n in nodes if isinstance(n, u.MaterialExpressionCustom)}
    coverage_description = 'Exact persistent cleanable coffee mask'
    normal_description = 'Subtle thin-film meniscus; no large raised pigment islands'
    require(coverage_description in customs and normal_description in customs, 'Accepted mask/normal nodes missing')
    coverage, normal = customs[coverage_description], customs[normal_description]
    for description, node in ((coverage_description, coverage), (normal_description, normal)):
        require(str(node.get_editor_property('code')) == source_code(description), 'Accepted '+description+' code changed')
        require('UV' in [str(entry.get_editor_property('input_name')) for entry in node.get_editor_property('inputs')],
                'Expected existing UV input')
    original_uvs = [node for node in nodes if isinstance(node, u.MaterialExpressionTextureCoordinate)]
    require(len(original_uvs) == 1, 'Review multiple coordinate sources before authoring')
    original_uv = original_uvs[0]
    old = customs.get(OLD_DESCRIPTION)
    old_strengths = [node for node in nodes if isinstance(node, u.MaterialExpressionScalarParameter)
                 and str(node.get_editor_property('parameter_name')) == 'CoffeeEdgeReconstruction']
    feather, combine = customs.get(FEATHER_DESCRIPTION), customs.get(COMBINE_DESCRIPTION)
    strengths = [node for node in nodes if isinstance(node, u.MaterialExpressionScalarParameter)
                 and str(node.get_editor_property('parameter_name')) == 'CoffeeEdgeSoftness']
    dithers = [node for node in nodes if isinstance(node, u.MaterialExpressionMaterialFunctionCall)
               and str(node.get_editor_property('desc')) == DITHER_DESCRIPTION]
    revision = lib.get_metadata_tag(material, 'MCCoffeeEdgeRevision')
    if revision == 'coffee_edge_reconstruction_v1:1':
        require(old is not None and len(old_strengths) == 1 and str(old.get_editor_property('code')) == OLD_CODE
                and lib.get_metadata_tag(material, TAG) == 'cardinal_mask_v1'
                and not feather and not combine and not strengths and not dithers,
                'Preserve unowned or partial cardinal nodes')
    elif revision == REVISION:
        require(feather is not None and combine is not None and len(strengths) == 1 and len(dithers) == 1
                and str(feather.get_editor_property('code')) == FEATHER_CODE
                and str(combine.get_editor_property('code')) == COMBINE_CODE
                and lib.get_metadata_tag(material, TAG) == OWNER and old is None and not old_strengths,
                'Preserve unowned or partial temporal-edge nodes')
    else:
        require(not revision and not lib.get_metadata_tag(material, TAG) and old is None
                and not old_strengths and feather is None and combine is None and not strengths and not dithers,
                'Preserve unexpected edge revision')
    function = lib.load_asset(FUNCTION)
    require(function is not None, 'Required existing Engine DitherTemporalAA function unavailable')
    disk = ROOT/('Content/'+PATH.removeprefix('/Game/')+'.uasset')
    report = dict(status='plan', revision=REVISION, material=PATH,
        appearance_revision='coffee_stain_film_v3:5', before_sha256=hashlib.sha256(disk.read_bytes()).hexdigest(),
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), before_nodes=len(nodes),
        before_opacity_node=edit.get_material_property_input_node(material, u.MaterialProperty.MP_OPACITY_MASK).get_name(),
        removed_cardinal_nodes=[node.get_name() for node in ([old]+old_strengths) if node is not None],
        modified_inputs=[coverage.get_name()+'.UV', normal.get_name()+'.UV', 'Material.OpacityMask'],
        mask_contract='Native bilinear UV and original mask/Finish restored. Only rendered opacity near coverage .25 is temporally feathered; zero coverage is explicitly invisible and coverage>=.5 fully opaque. Native mask bytes and brush semantics unchanged.',
        geometry_contract='No geometry, native mask, brush, texture upload or cell-resolution changes',
        wipe_texture_reads=5, native_function=FUNCTION, extra_layers=0,
        native_noise_texture='/Engine/EngineMaterials/Good64x64TilingNoiseHighFreq',
        shader_cost_note='Five explicit WipeMask reads retained. Native DitherTemporalAA contains a noise texture sample and temporal arithmetic; total compiled texture/instruction and GPU costs require measurement.',
        feather='Target 1.5 screen pixels, full world-width clamped2..4cm, alpha half-width capped .24 to retain exact-zero invisibility. Edge softness0=original hard clip,1=native temporal feather.',
        previous_review='Cardinal edge1 rejected in actual 20261017T022734Z run: stronger flat zigzags instead of softness.',
        gpu_cost='unmeasured', gameplay_visual_review='pending',
        references=['https://dev.epicgames.com/documentation/unreal-engine/stylized-rendering-materials?application_version=4.27',
                    'https://dev.epicgames.com/documentation/unreal-engine/anti-aliasing-and-upscaling-in-unreal-engine?lang=en-US'])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    if OUT.exists():
        previous = OUT.read_bytes()
        history = OUT.parent/'History'/(hashlib.sha256(previous).hexdigest()+'.json')
        history.parent.mkdir(parents=True, exist_ok=True)
        if not history.exists():
            history.write_bytes(previous)
    if globals().get('MC_COFFEE_EDGE_APPLY', False):
        backup = OUT.parent/'Backup'/report['before_sha256']/disk.name
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(disk, backup)
        report['backup'] = str(backup)
        OUT.write_text(json.dumps(report, indent=2), encoding='utf-8')
        require(edit.connect_material_expressions(original_uv, '', coverage, 'UV'), 'Original coverage UV reconnect failed')
        require(edit.connect_material_expressions(original_uv, '', normal, 'UV'), 'Original normal UV reconnect failed')
        for node in ([old]+old_strengths):
            if node is not None:
                edit.delete_material_expression(material, node)
        if feather is None:
            graph = Graph(material)
            graph.row = 4600
            strength = graph.scalar('CoffeeEdgeSoftness', 1.)
            positions = [node for node in nodes if isinstance(node, u.MaterialExpressionWorldPosition)]
            require(len(positions) == 1, 'Review ambiguous world-position source before authoring')
            feather = graph.custom(FEATHER_DESCRIPTION, FEATHER_CODE, dict(Coverage=coverage, P=positions[0]))
            dither = graph.node(u.MaterialExpressionMaterialFunctionCall)
            dither.set_editor_property('desc', DITHER_DESCRIPTION)
            require(dither.set_material_function(function), 'Native temporal function assignment failed')
            names = [str(name) for name in edit.get_material_expression_input_names(dither)]
            alpha_names = [name for name in names if 'alpha' in name.lower()]
            require(len(alpha_names) == 1, 'Expected native Alpha Threshold input, got '+str(names))
            require(edit.connect_material_expressions(feather, '', dither, alpha_names[0]), 'Native alpha input reconnect failed')
            combine = graph.custom(COMBINE_DESCRIPTION, COMBINE_CODE,
                                   dict(Coverage=coverage, Temporal=dither, Strength=strength))
        else:
            dither = dithers[0]
            require(dither.get_editor_property('material_function') == function, 'Native temporal function changed')
            names = [str(name) for name in edit.get_material_expression_input_names(dither)]
        report['native_function_input_names'] = names
        require(edit.connect_material_property(combine, '', u.MaterialProperty.MP_OPACITY_MASK), 'Opacity reconnect failed')
        errors = edit.recompile_material(material)
        require(not errors, 'Edge material compile errors: '+str(errors))
        lib.set_metadata_tag(material, TAG, OWNER)
        lib.set_metadata_tag(material, 'MCCoffeeEdgeRevision', REVISION)
        require(lib.save_loaded_asset(material, False), 'Could not save owned edge patch')
        report.update(status='authored', compiled=True, after_nodes=edit.get_num_material_expressions(material),
                      after_sha256=hashlib.sha256(disk.read_bytes()).hexdigest())
    report['utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    OUT.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
