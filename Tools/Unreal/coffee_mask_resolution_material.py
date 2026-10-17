"""Build the resolution experiment in /Engine/Transient; never save packages.

The production rev5 appearance and Edge2 author blocks are reused unchanged.
Only the four normal-neighbour offsets gain a MaskResolution scalar. Five
explicit wipe reads and the native temporal-noise function remain present.
Import as support, then call build(); this file does not start PIE.
"""
import hashlib
import runpy
import time
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
CURRENT = '/Game/Cataclysm/Jelly/M_CataclysmPaintTrail'
REVISION = 'coffee_mask_resolution_lab:1'
edit, lib = u.MaterialEditingLibrary, u.EditorAssetLibrary


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def dirty_packages():
    return dict(content=sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
                maps=sorted(p.get_path_name() for p in u.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


def protected_packages():
    paths = ('Content/Cataclysm/Jelly/M_CataclysmPaintTrail.uasset',
             'Content/Gameplay/Liquid/Coffee/M_CoffeePuddleFilm.uasset',
             'Content/Gameplay/Liquid/MI_CoffeePuddle.uasset',
             'Content/Gameplay/Liquid/M_CoffeePuddle.uasset',
             'Content/Gameplay/Liquid/MI_FoodSaucePuddle.uasset',
             'Content/Gameplay/Liquid/MI_CurryPuddle.uasset',
             'Content/Gameplay/Liquid/MI_VomitPuddle.uasset')
    return {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
            for name in paths if (ROOT/name).is_file()}


def build():
    require(Path(u.Paths.get_project_file_path()).stem == 'MessControl', 'Wrong project')
    before_dirty, before_packages = dirty_packages(), protected_packages()
    current = lib.load_asset(CURRENT)
    require(lib.get_metadata_tag(current, 'MCCoffeeSurfaceRevision') == 'coffee_stain_film_v3:5'
            and lib.get_metadata_tag(current, 'MCCoffeeEdgeRevision') == 'coffee_edge_reconstruction_v1:2',
            'Resolution lab requires the accepted saved rev5 + Edge2')
    appearance_path = ROOT/'Tools/Unreal/author_coffee_stain_film_v3.py'
    edge_path = ROOT/'Tools/Unreal/author_coffee_stain_edges_v1.py'
    appearance = runpy.run_path(str(appearance_path), run_name='resolution_appearance_support')
    edges = runpy.run_path(str(edge_path), run_name='resolution_edge_support')
    require(appearance['REVISION'] == 'coffee_stain_film_v3:5'
            and edges['REVISION'] == 'coffee_edge_reconstruction_v1:2', 'Author recipe revision changed')
    material = u.new_object(u.Material, name='MCCoffeeMaskResolution_'+str(time.time_ns()))
    require(material.get_path_name().startswith('/Engine/Transient.'), 'Material must be transient')
    appearance['author'](material)
    nodes = list(edit.get_material_expressions(material))
    customs = {str(n.get_editor_property('description')): n for n in nodes
               if isinstance(n, u.MaterialExpressionCustom)}
    normal = customs['Subtle thin-film meniscus; no large raised pigment islands']
    coverage = customs['Exact persistent cleanable coffee mask']
    code = str(normal.get_editor_property('code'))
    require(code.count('float e=1.0/64.0;') == 1, 'Expected exact accepted normal offset')
    normal.set_editor_property('code', code.replace('float e=1.0/64.0;',
        'float e=1.0/max(MaskResolution,1.0);'))
    entries = list(normal.get_editor_property('inputs'))
    entry = u.CustomInput()
    entry.set_editor_property('input_name', 'MaskResolution')
    entries.append(entry)
    normal.set_editor_property('inputs', entries)
    graph = appearance['Graph'](material)
    graph.row = 4600
    resolution = graph.scalar('MaskResolution', 64.)
    require(edit.connect_material_expressions(resolution, '', normal, 'MaskResolution'),
            'Normal resolution input did not connect')
    positions = [n for n in nodes if isinstance(n, u.MaterialExpressionWorldPosition)]
    require(len(positions) == 1, 'Expected one unchanged world-position input')
    feather = graph.custom(edges['FEATHER_DESCRIPTION'], edges['FEATHER_CODE'],
                           dict(Coverage=coverage, P=positions[0]))
    function = lib.load_asset(edges['FUNCTION'])
    require(function is not None, 'Native temporal function unavailable')
    dither = graph.node(u.MaterialExpressionMaterialFunctionCall)
    dither.set_editor_property('desc', edges['DITHER_DESCRIPTION'])
    require(dither.set_material_function(function), 'Native dither assignment failed')
    names = [str(n) for n in edit.get_material_expression_input_names(dither)]
    alpha = [name for name in names if 'alpha' in name.lower()]
    require(len(alpha) == 1, 'Ambiguous native dither alpha input: '+str(names))
    require(edit.connect_material_expressions(feather, '', dither, alpha[0]), 'Dither alpha connection failed')
    combine = graph.custom(edges['COMBINE_DESCRIPTION'], edges['COMBINE_CODE'],
        dict(Coverage=coverage, Temporal=dither, Strength=graph.scalar('CoffeeEdgeSoftness', 1.)))
    graph.out(combine, u.MaterialProperty.MP_OPACITY_MASK)
    errors = list(edit.recompile_material(material))
    require(not errors, 'Transient resolution material compile failed: '+str(errors))
    require(dirty_packages() == before_dirty, 'Transient authoring changed editor dirty packages')
    require(protected_packages() == before_packages, 'Production coffee packages changed')
    report = dict(revision=REVISION, path=material.get_path_name(), compiled=True,
        appearance_revision=appearance['REVISION'], edge_revision=edges['REVISION'],
        normal_resolution_code=str(normal.get_editor_property('code')),
        mask_reads=5, native_dither_noise_texture=True, extra_layers=0,
        scalar_parameters=[str(n) for n in edit.get_scalar_parameter_names(material)],
        vector_parameters=[str(n) for n in edit.get_vector_parameter_names(material)],
        source_sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in (Path(__file__), appearance_path, edge_path)},
        protected_packages=before_packages, dirty_packages=before_dirty,
        contract='Accepted rev5+Edge2, unchanged samples/style/geometry; normal e=1/MaskResolution only')
    try:
        stats = edit.get_statistics(material)
        report['compiled_statistics'] = {name: int(stats.get_editor_property(name)) for name in
            ('num_vertex_shader_instructions', 'num_pixel_shader_instructions', 'num_samplers',
             'num_vertex_texture_samples', 'num_pixel_texture_samples')}
    except Exception as caught:
        report['compiled_statistics'] = dict(unavailable=str(caught))
    return material, report


if __name__ == '__main__':
    material, report = build()
    print(report)
