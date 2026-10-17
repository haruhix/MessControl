import runpy
from pathlib import Path
import unreal as u
runpy.run_path(str(Path(u.Paths.project_dir(),'Tools/Unreal/validate_nut_tank_v4.py')),
              init_globals={'MC_TANK_V4_PROBE_CASE':'Roll'})
