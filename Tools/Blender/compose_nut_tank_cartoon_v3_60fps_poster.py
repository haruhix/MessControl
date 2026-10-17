"""Create a compact poster from independently rendered 60 fps source frames."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/NutAnimationStyle/TankCartoonV3'
CACHE = ROOT / 'Saved/NutAnimationStyle/TankCartoonV3_60fps'
FONT = 'C:/Windows/Fonts/arial.ttf'


def main():
    poster = Image.new('RGB', (1024, 1320), (25, 30, 39))
    draw = ImageDraw.Draw(poster)
    title = ImageFont.truetype(FONT, 32)
    text = ImageFont.truetype(FONT, 24)
    small = ImageFont.truetype(FONT, 18)
    draw.text((24, 16), 'Tank | smooth cartoon curves | actual 60 FPS', font=title, fill='white')
    draw.text((24, 58), 'Each frame uses fractional Blender evaluation. Same rig and camera.', font=small,
              fill=(186, 197, 214))
    for clip, frame, y, description in (
        ('Walk', 14, 100, '1.92s cycle | grounded broad gait'),
        ('Melee', 39, 510, '0.65s windup / 2.50s recovery | contact at 50%')):
        draw.text((24, y), clip + ' | ' + description, font=text, fill=(237, 194, 110))
        for column, version in enumerate(('Old', 'New')):
            picture = Image.open(CACHE / (clip + '_' + version) / f'{frame:04d}.png').convert('RGB')
            picture = picture.resize((368, 368), Image.Resampling.LANCZOS)
            x = 72 + column * 512
            poster.paste(picture, (x, y + 32))
            draw.text((x + 10, y + 40), 'Previous' if version == 'Old' else 'Smooth cartoon',
                      font=small, fill='white')
    draw.text((24, 925), 'Hit | 0.40s | compression / rebound / settle', font=text, fill=(237, 194, 110))
    for column, (frame, label) in enumerate(((5, 'Compression'), (10, 'Rebound'), (16, 'Settle'))):
        picture = Image.open(CACHE / 'Hit_New' / f'{frame:04d}.png').convert('RGB')
        picture = picture.resize((300, 300), Image.Resampling.LANCZOS)
        x = 20 + column * 342
        poster.paste(picture, (x, 965))
        draw.text((x + 10, 975), label, font=small, fill='white')
    draw.text((24, 1290), 'Blender source preview; final game rendering and collision are evaluated in Unreal.',
              font=small, fill=(186, 197, 214))
    path = OUT / 'Tank_Cartoon_Comparison_60fps.png'
    poster.save(path)
    print(path)


if __name__ == '__main__':
    main()
