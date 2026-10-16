"""Export all corrected knight actions without modifying the supplied Blender file."""
import sys
import json
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_nut_knight_ball as pipeline

pipeline.SOURCE = Path('C:/Users/vasil/Downloads/Telegram Desktop/Nut_Tank2 (3).blend')
pipeline.OUT = pipeline.ROOT / 'ArtSource/NutKnightCorrected'

if __name__ == '__main__':
    pipeline.main()
    report_path = pipeline.OUT / 'ExportReport.json'
    report = json.loads(report_path.read_text())
    report['scope'] = 'All fourteen corrected actions; reimport active animation assets in place. Preserve existing skeleton, meshes, locomotion, profile and gameplay tuning.'
    report_path.write_text(json.dumps(report, indent=2))
