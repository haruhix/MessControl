"""Encode the one local gameplay review without requiring other scenarios."""
import json
from pathlib import Path

from BuildNutCombatVideo import read_capture, encode


def main():
    directory = Path(__file__).resolve().parents[1] / 'Artifacts/WizardPerformanceV7/PIE_Final'
    groups = [('Wizard_Fireball_V7_PIE', ['WizardFireball'])]
    report, scenarios, dimensions, cadence = read_capture(directory, groups)
    videos = [encode(directory, name, cases, scenarios, dimensions) for name, cases in groups]
    output = dict(videos=videos, cadence=cadence, review_profile=report['review_profile'],
                  scenarios=report['scenarios'], restore_errors=report['restore_errors'])
    (directory/'Video.json').write_text(json.dumps(output, indent=2), encoding='utf8')
    print(json.dumps(output, indent=2))


if __name__ == '__main__':
    main()
