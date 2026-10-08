"""Import/QA only; does not require new native slots or change DA_NutRain.

Run in the current Editor:
    py "E:/DEVGAME/MessControl/Tools/Unreal/import_nut_style_v2.py"
The complete post-build connection is integrate_nut_style_v2.py.
"""
import importlib.util
from pathlib import Path

module_path = Path(__file__).resolve().with_name("integrate_nut_style_v2.py")
spec = importlib.util.spec_from_file_location("mc_nut_style_v2_import", module_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.main(import_only=True)
