"""Label actual Blender renders without changing their pixels or framing."""
from pathlib import Path
from PIL import Image, ImageDraw

BASE = Path(__file__).resolve().parent / 'review'

def sheet(rows, output, width=256, height=256):
    columns = max(map(len, rows))
    canvas = Image.new('RGB', (columns*width, len(rows)*(height+28)), '#202426')
    draw = ImageDraw.Draw(canvas)
    for row, entries in enumerate(rows):
        for column, (file, label) in enumerate(entries):
            image = Image.open(BASE/file).convert('RGB')
            image.thumbnail((width, height), Image.Resampling.LANCZOS)
            x = column*width+(width-image.width)//2
            y = row*(height+28)
            canvas.paste(image, (x, y+(height-image.height)//2))
            draw.text((column*width+8, y+height+5), label, fill='white')
    canvas.save(BASE/output)

sheet([
    [('three_quarter.png', 'Hero 3/4'), ('front.png', 'Front +X'), ('side.png', 'Left -Y')],
    [('back.png', 'Back -X'), ('right.png', 'Right +Y'), ('top.png', 'Top +Z')],
    [('gameplay.png', 'Elevated gameplay view')],
], 'multiview_contact_sheet.png', 400, 300)

motion = {
    'Idle': [1,31,61,91,121],
    'Hop': [1,15,20,44,63,71,91],
    'Hurt': [1,5,12,20,29],
    'PreBurst': [1,16,31,46,61],
    'Burst': [1,5,10,19],
}
sheet([[(f'A_CoffeeSlime_{role}_{frame:03}.png', f'{role}  {(frame-1)/100:.2f}s')
        for frame in frames] for role,frames in motion.items()], 'motion_contact_sheet.png')
if (BASE/'fresh_glb_Idle.png').exists():
    sheet([
        [('hero.png','Source Idle'),('A_CoffeeSlime_Hop_044.png','Source Hop apex'),('A_CoffeeSlime_PreBurst_061.png','Source PreBurst end')],
        [('fresh_glb_Idle.png','Fresh GLB Idle'),('fresh_glb_Hop.png','Fresh GLB Hop apex'),('fresh_glb_PreBurst.png','Fresh GLB PreBurst end')],
    ], 'fresh_glb_comparison.png',400,300)
print(BASE/'multiview_contact_sheet.png')
print(BASE/'motion_contact_sheet.png')
