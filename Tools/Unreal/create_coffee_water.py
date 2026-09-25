"""Compatibility entry point; the current coffee includes pouring and draining."""
from pathlib import Path
script=Path(__file__).with_name('create_coffee_pour.py')
exec(compile(script.read_text(encoding='utf-8'),str(script),'exec'),{'__file__':str(script),'__name__':'__main__'})
