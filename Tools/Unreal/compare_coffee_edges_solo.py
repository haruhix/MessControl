"""Same native wave patch/camera: hard mask vs native temporal thin-band feather.

Root-run only after the scoped edge author. One genuine PIE; no native dirt,
mask, timing or AI changes. Each candidate gets three static seconds and a small
orbit, then original native gates and normal brushing run with restored values.
This compares native masked temporal coverage, with no translucent layer.
"""
import runpy
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir()).resolve()
runpy.run_path(str(ROOT/'Tools/Unreal/compare_coffee_film_pbr_solo.py'), run_name='__main__',
    init_globals={
        'MC_COFFEE_PBR_OUTPUT': ROOT/'Saved/CoffeeStainEdges/Capture',
        'MC_COFFEE_COMPARE_AFTER_WIPE': True,
        'MC_COFFEE_REQUIRE_STABLE_MASK': True,
        'MC_COFFEE_COMPARE_PURPOSE': 'Same real wave patch: original hard clip vs native temporal thin-band coverage; real brushing afterwards',
        'MC_COFFEE_COMPARE_LABELS': {
            'coffee_edge_hard': 'Край пятна: исходная жёсткая маска',
            'coffee_edge_temporal': 'Край пятна: native DitherTemporalAA'},
        'MC_COFFEE_COMPARE_VARIANTS': (
            ('coffee_edge_hard', {'CoffeeRoughness': .085, 'CoffeeMicroNormalStrength': .0025,
                                 'CoffeeEdgeSoftness': 0.}),
            ('coffee_edge_temporal', {'CoffeeRoughness': .085, 'CoffeeMicroNormalStrength': .0025,
                                     'CoffeeEdgeSoftness': 1.}),
        )})
