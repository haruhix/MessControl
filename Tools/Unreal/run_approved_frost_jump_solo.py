"""Enable short actual frames for the owned frost jump probe."""
from pathlib import Path
import unreal

source = Path(unreal.Paths.project_dir())/'Tools/Unreal/check_approved_frost_jump_solo.py'
exec(compile(source.read_text(encoding='utf8'), str(source), 'exec'),
     {'__name__': '__main__', 'MC_FROST_CAPTURE_HZ': 6})
