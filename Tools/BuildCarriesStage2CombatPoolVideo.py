"""Compose the new Stage 2 combat pool from complete Blender render records.

Accepts one or multiple views in manifest order. Durations and FPS are read from
RenderManifest; this wrapper adds no frames, resampling, or view-count rules.
"""
import importlib
from pathlib import Path


def main():
    video = importlib.import_module('BuildCarriesStage2ReviewVideo')
    video.OUTPUT = Path(__file__).resolve().parents[1] / 'Artifacts/CarriesStage2/CombatPool_v1'
    video.TITLE = 'Кариес · фаза 2 · новые анимации'
    video.BASENAME = 'Carries_Stage2_NewAnimations'
    video.VIEW_LABELS = {'ThreeQuarter': 'Три четверти', 'Front': 'Спереди', 'Side': 'Сбоку'}
    video.main()


if __name__ == '__main__':
    main()
