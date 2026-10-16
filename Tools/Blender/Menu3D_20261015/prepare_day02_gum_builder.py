"""Reuse validated DCC authoring boilerplate, never DAY01 geometry or UV data."""
from pathlib import Path
HERE = Path(__file__).resolve().parent
source = (HERE/'build_day01_gums_source_locked.py').read_text(encoding='utf8')
replacements = {
    'DAY01': 'DAY02', 'day01': 'day02',
    "prep_report['source_image']": "prep_report['source']",
    'if len(crowns) < 10:': 'if len(crowns) != 23:',
    'Need all ten observed DAY02 upper crowns': 'Need exactly 11 observed upper and 12 observed lower DAY02 crowns',
    '(963, 212)': '(963, 217)', '[963, 212]': '[963, 217]',
}
for old, new in replacements.items():
    if old not in source:
        raise RuntimeError('DCC boilerplate changed; review before deriving DAY02: '+old)
    source = source.replace(old, new)
old = "original_crown_hashes = {obj.name: mesh_digest(obj) for obj in crowns}"
new = """rows_found = collections.Counter(str(obj.get('SourceRow', '')).lower() for obj in crowns)
if rows_found != {'upper': 11, 'lower': 12}:
    raise RuntimeError('Source DAY02 upper/lower split differs from observed23: ' + str(rows_found))
original_crown_hashes = {obj.name: mesh_digest(obj) for obj in crowns}"""
if old not in source:
    raise RuntimeError('Missing verified crown snapshot insertion point')
source = source.replace(old, new)
source = source.replace("'Compound source ownership, actual lower-white mask and UV raster audit still need review.'",
                        "'Compound per-part source ownership, upper/lower cervical rays and UV raster audit still need review.'")
path = HERE/'build_day02_gums_source_locked.py'
path.write_text(source, encoding='utf8')
print('DAY02_DCC_AUTHOR_SOURCE_READY_NOT_RUN', path, flush=True)
