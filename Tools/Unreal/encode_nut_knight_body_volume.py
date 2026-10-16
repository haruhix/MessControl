"""Encode the passing volume-preserving native Knight gameplay capture."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import encode_nut_knight_corrected as encoder

encoder.OUT=encoder.ROOT/'Artifacts/NutKnightBodyFix_20261017'
if __name__=='__main__':encoder.main()
