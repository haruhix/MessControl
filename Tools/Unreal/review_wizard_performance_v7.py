"""Record one real local Fireball attack with the isolated V7 profile."""
from pathlib import Path
import unreal as u
MC_NUT_COMBAT_OUTPUT=str(Path(u.Paths.project_dir(),'Artifacts/WizardPerformanceV7/PIE_Final'))
MC_NUT_COMBAT_CASES=[('WizardFireball',9,126)]
MC_NUT_COMBAT_NUT_PROFILE='/Game/Gameplay/CoreLoop/NutWizard/MCPerformanceV7/DA_NutRain_PerformanceV7'
exec(compile(Path(u.Paths.project_dir(),'Tools/Unreal/capture_nut_combat_v5.py').read_text(encoding='utf8'),
             'capture_nut_combat_v5.py','exec'),globals())
