"""Compose the two camera views of the hand-supported locomotion study."""
from pathlib import Path
import BuildCarriesStage2ReviewVideo as video

video.OUTPUT=Path(__file__).resolve().parents[1]/'Artifacts/CarriesStage2/HandSupportWalk_v4'
video.BASENAME='Carries_Stage2_HandSupportWalk'
video.TITLE='Босс · фаза 2 · походка с опорой на руку'
video.VIEW_LABELS={'ThreeQuarter':'Три четверти','Side':'Сбоку'}

if __name__=='__main__': video.main()
