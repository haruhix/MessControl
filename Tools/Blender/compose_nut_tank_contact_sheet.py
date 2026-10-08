"""Compose the Tank ability poses without changing their aspect ratios."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/Tank'
POSES = [
    ('Melee_033.png', 'Melee / anticipation'),
    ('Melee_051.png', 'Melee / contact'),
    ('Tank_v2_ChargeTell_Coil.png', 'ChargeTell / brace'),
    ('Tank_v2_ChargeLoop.png', 'ChargeLoop / sprint'),
    ('Tank_v2_ChargeRecovery_Stop.png', 'ChargeRecovery / brake'),
    ('Jump_023.png', 'Jump / load'),
    ('Jump_052.png', 'Jump / airborne'),
    ('Jump_071.png', 'Jump / landing'),
    ('Tank_v2_RollTransform_Coil.png', 'RollTransform / tuck'),
    ('Tank_v2_Rolling_Ball.png', 'Rolling / actual ball'),
    ('Tank_v2_Rolling_Ricochet.png', 'Rolling / edge turn'),
    ('Tank_v2_Unfold_Guard.png', 'Unfold / recovery'),
]


def main():
    sheet = Image.new('RGB', (1440, 1008), (24, 30, 40))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 19)
    for i, (filename, label) in enumerate(POSES):
        x, y = (i % 4) * 360, (i // 4) * 336
        with Image.open(OUT / filename) as source:
            tile = ImageOps.contain(source.convert('RGB'), (360, 303), Image.Resampling.LANCZOS)
        sheet.paste(tile, (x + (360 - tile.width) // 2, y + (303 - tile.height) // 2))
        draw.text((x + 10, y + 309), label, font=font, fill=(240, 244, 255))
    sheet.save(OUT / 'Tank_AllAbilities_v2_ContactSheet.png')


if __name__ == '__main__':
    main()
