"""Launch the installed codebase-memory server with an explicit project working directory."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--project", type=Path, required=True)
parser.add_argument("--binary", type=Path, required=True)
args = parser.parse_args()
os.chdir(args.project.resolve())
os.environ["CBM_ALLOWED_ROOT"] = str(args.project.resolve())
raise SystemExit(subprocess.call([str(args.binary.resolve())]))
