"""Import the bounded lower-jaw readability revision with the shared native contract."""
from pathlib import Path
import runpy
import unreal as u

request={'revision':'v07_LowerRowReadability','mesh_name':'SK_DAY03_LowerReadable_v07',
    'fbx_sha256':'bbbc62e2eb6ec8c325d6ac69edd5e6c25513e7c03742c095c2c6f272c7bf8f08',
    'report_name':'lower_readable_v07_import_report.json'}
script=Path(u.Paths.project_dir())/'Tools/Unreal/Menu3D/import_vampire_symmetric_v06.py'
runpy.run_path(str(script),init_globals={'MC_DAY03_IMPORT_REQUEST':request})
