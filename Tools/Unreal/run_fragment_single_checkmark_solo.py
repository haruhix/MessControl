"""Record the focused stack delivery check after feedback aggregation."""
from pathlib import Path
import unreal as u

source = Path(u.Paths.project_dir())/'Tools/Unreal/check_fragment_stack_solo.py'
exec(compile(source.read_text(encoding='utf8'), str(source), 'exec'),
     {'__name__': '__main__',
      'MC_FRAGMENT_OUTPUT': Path(u.Paths.project_saved_dir())/'Checks/FragmentSingleCheckmarkSolo',
      'MC_FRAGMENT_COLLISION': False,
      'MC_FRAGMENT_CAPTURE_HZ': 6})
