"""Capture the V4 baseline before rebuilding the new native presentation."""
import runpy
from pathlib import Path
import unreal as u
runpy.run_path(str(Path(u.Paths.project_dir(), 'Tools/Unreal/capture_nut_tank_iteration_v4.py')),
              init_globals={'MC_TANK_ITERATION_VERSION': 'Before'})
