"""Run the current pond rules regression for the hub fishing rod."""
import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).with_name('check_fishing_pond_solo.py')))