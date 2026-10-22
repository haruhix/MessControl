"""Branch9 original vector source + deterministic supersampled UI rasters.

Requires Python 3.10+, Pillow and fontTools. No fonts, downloaded art or
platform SVG rasterizer are used; paths remain editable in Branch9.svg.
"""
from pathlib import Path
import json, math, hashlib, sys
from PIL import Image, ImageDraw, ImageChops
from fontTools.pens.basePen import BasePen
from fontTools.svgLib.path import parse_path

ROOT = Path(__file__).resolve().parent
OUT = Path('E:/CodexWork/MessControlFrontEndBranch9_20261021/Art')
W, H, SS = 2048, 768, 3
OUT.mkdir(parents=True, exist_ok=True)

# Bespoke bold geometric outlines. Coordinates are design units, not glyphs
# copied from a font. Round bowls and cut counters share consistent weights.
GLYPHS = [
 ('B',210, 'M0 0 L133 0 C181 0 200 22 200 60 C200 84 190 102 173 113 C196 122 208 143 208 171 C208 217 180 246 132 246 L0 246 Z M57 49 L57 94 L124 94 C141 94 149 86 149 72 C149 56 140 49 124 49 Z M57 143 L57 196 L129 196 C147 196 156 186 156 169 C156 152 147 143 129 143 Z'),
 ('R',442, 'M0 0 L132 0 C184 0 208 29 208 78 C208 113 193 138 166 149 L218 246 L151 246 L107 159 L57 159 L57 246 L0 246 Z M57 49 L57 110 L124 110 C144 110 153 100 153 79 C153 59 144 49 124 49 Z'),
 ('A',680, 'M82 0 L142 0 L231 246 L170 246 L154 197 L69 197 L53 246 L-7 246 Z M111 66 L83 149 L139 149 Z'),
 ('N',932, 'M0 0 L55 0 L154 153 L154 0 L211 0 L211 246 L155 246 L57 94 L57 246 L0 246 Z'),
 ('C',1170, 'M208 33 L174 75 C156 56 140 48 116 48 C77 48 56 75 56 123 C56 171 78 199 117 199 C142 199 160 189 177 169 L211 208 C188 237 157 251 115 251 C43 251 0 202 0 123 C0 45 44 -5 116 -5 C155 -5 185 8 208 33 Z'),
 ('H',1405, 'M0 0 L57 0 L57 94 L151 94 L151 0 L208 0 L208 246 L151 246 L151 146 L57 146 L57 246 L0 246 Z'),
]
Y = 290
NINE = 'M1640 309 C1640 230 1683 185 1751 185 C1823 185 1864 231 1864 298 C1864 338 1850 372 1827 409 L1737 548 L1657 548 L1748 414 C1739 417 1727 419 1718 418 C1668 413 1640 369 1640 309 Z M1701 302 C1701 337 1718 356 1751 356 C1784 356 1803 336 1803 302 C1803 268 1785 247 1752 247 C1719 247 1701 269 1701 302 Z'

# Cut tapers into the branch root; asymmetric joins avoid a ruler-straight
# slash. The white core continues into the negative space outside the letters.
CUT = 'M650 581 L845 505 L1006 443 L1138 397 L1285 343 L1400 288 L1509 238 L1639 167 L1605 211 L1520 266 L1413 319 L1300 370 L1151 420 L1017 462 L852 520 Z'
STEM = 'M601 600 C753 545 868 491 1007 446 C1150 401 1289 352 1406 300 C1514 252 1586 204 1651 160 C1612 202 1520 270 1410 312 C1288 365 1155 411 1013 455 C864 500 755 550 601 600 Z'
TWIGS = [
 'M790 529 C775 536 757 546 741 557 C754 541 771 533 790 529 Z',
 'M1132 413 C1148 380 1167 350 1189 328 C1166 366 1156 390 1150 408 Z',
 'M1287 359 C1290 313 1307 270 1333 244 C1310 284 1306 322 1304 352 Z',
 'M1392 312 C1432 321 1467 317 1499 303 C1475 324 1432 334 1400 323 Z',
 'M1504 265 C1502 223 1511 191 1530 165 C1516 203 1516 230 1518 256 Z',
 'M1582 215 C1627 216 1665 197 1690 170 C1665 207 1628 227 1596 229 Z',
 'M890 503 C867 497 846 491 822 491 C845 483 866 483 909 496 Z',
]
LEAVES = [
 'M746 550 C736 573 715 583 693 577 C702 554 722 543 746 550 Z',
 'M1186 332 C1194 307 1215 293 1239 297 C1231 321 1210 337 1186 332 Z',
 'M1333 250 C1308 232 1300 203 1307 181 C1338 190 1356 219 1333 250 Z',
 'M1495 306 C1512 280 1534 272 1558 279 C1542 304 1520 316 1495 306 Z',
 'M1527 173 C1540 150 1562 134 1587 137 C1580 163 1556 182 1527 173 Z',
 'M1683 178 C1689 151 1706 131 1736 128 C1730 156 1712 180 1683 178 Z',
 'M825 493 C810 512 790 522 768 515 C777 491 798 480 825 493 Z',
]

class PolyPen(BasePen):
 def __init__(self, transform=None):
  super().__init__(None); self.contours=[]; self.cur=[]; self.pos=(0,0); self.transform=transform or (lambda x,y:(x,y))
 def point(self,p): self.cur.append(self.transform(*p)); self.pos=p
 def _moveTo(self,p):
  if self.cur: self.contours.append(self.cur)
  self.cur=[]; self.point(p)
 def _lineTo(self,p): self.point(p)
 def _curveToOne(self,a,b,c):
  p=self.pos
  n=max(8,int((math.dist(p,a)+math.dist(a,b)+math.dist(b,c))/5))
  for i in range(1,n+1):
   t=i/n;s=1-t
   self.point((s*s*s*p[0]+3*s*s*t*a[0]+3*s*t*t*b[0]+t*t*t*c[0],s*s*s*p[1]+3*s*s*t*a[1]+3*s*t*t*b[1]+t*t*t*c[1]))
 def _closePath(self):
  if self.cur: self.contours.append(self.cur); self.cur=[]
 def _endPath(self): self._closePath()

def mask_path(d, transform=None):
 pen=PolyPen(transform); parse_path(d,pen); pen._endPath()
 mask=Image.new('1',(W*SS,H*SS))
 for contour in pen.contours:
  layer=Image.new('1',mask.size); ImageDraw.Draw(layer).polygon([(x*SS,y*SS) for x,y in contour],fill=1)
  mask=ImageChops.logical_xor(mask,layer)
 return mask.convert('L')

def union(paths):
 out=Image.new('L',(W*SS,H*SS))
 for d in paths: out=ImageChops.lighter(out,mask_path(d))
 return out

solid=Image.new('L',(W*SS,H*SS))
for name,x,d in GLYPHS:
 glyph=mask_path(d,lambda px,py,ox=x:(px+ox-0.035*py,py+Y))
 solid=ImageChops.lighter(solid,glyph)
solid=ImageChops.lighter(solid,mask_path(NINE))
cut=mask_path(CUT)
word=ImageChops.subtract(solid,cut)
branch=union([STEM]+TWIGS)
leaves=union(LEAVES)
logo=ImageChops.lighter(ImageChops.lighter(word,branch),leaves)

def rgba(mask):
 im=Image.new('RGBA',mask.size,(255,255,255,0)); im.putalpha(mask);return im.resize((W,H),Image.Resampling.LANCZOS)

svghead=f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-labelledby="title desc">\n<title id="title">Branch9 studio</title>\n<desc id="desc">Original geometric white Branch9 wordmark intersected by an ascending organic branch and seven leaves. Editable vector paths; transparent background.</desc>\n'
glyphsvg='\n'.join(f'<path id="letter-{n}" transform="translate({x} {Y}) matrix(1 0 -0.035 1 0 0)" d="{d}"/>' for n,x,d in GLYPHS)+f'\n<path id="number-9" d="{NINE}"/>'
svg=svghead+f'<defs><mask id="branch-carve" maskUnits="userSpaceOnUse" x="0" y="0" width="{W}" height="{H}"><rect width="{W}" height="{H}" fill="white"/><path d="{CUT}" fill="black"/></mask></defs>\n<g id="Wordmark" fill="white" fill-rule="evenodd" mask="url(#branch-carve)">\n{glyphsvg}\n</g>\n<g id="Branch" fill="white">\n'+ '\n'.join(f'<path id="branch-{i}" d="{d}"/>' for i,d in enumerate([STEM]+TWIGS))+'\n</g>\n<g id="Leaves" fill="white">\n'+'\n'.join(f'<path id="leaf-{i+1}" d="{d}"/>' for i,d in enumerate(LEAVES))+'\n</g>\n</svg>\n'
(ROOT/'Branch9.svg').write_text(svg,encoding='utf-8')

layers={ 'T_Branch9_Wordmark':word, 'T_Branch9_Branch':branch, 'T_Branch9_Leaves':leaves, 'T_Branch9_Logo':logo, 'T_Branch9_SolidWordmark':solid, 'T_Branch9_RevealMask':cut }
manifest=[]
for name,mask in layers.items():
 im=rgba(mask); path=ROOT/(name+'.png'); im.save(path,optimize=True)
 manifest.append({'id':name,'file':path.name,'size':[W,H],'alpha_bbox':im.getchannel('A').getbbox(),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})

hero=Image.new('RGB',(W,H),(6,7,8));hero.paste(rgba(logo),mask=rgba(logo).getchannel('A'));hero.save(OUT/'Branch9_Hero.png')
scale=rgba(logo).resize((640,240),Image.Resampling.LANCZOS)
preview=Image.new('RGB',(768,384),(6,7,8));preview.paste(scale,(64,72),scale);preview.save(OUT/'Branch9_640px.png')
(ROOT/'AssetManifest.json').write_text(json.dumps({'asset':'Branch9StudioWordmark','date':'2026-10-21','revision':2,'status':'static-approved-by-root; runtime-integration-pending','creator':'Codex bespoke SVG path construction','reference':'User-provided Branch9 reference, copied only to task working folder','font_dependencies':[],'downloaded_art_dependencies':[],'canvas':[W,H],'anchor':[0.5,0.5],'palette':{'foreground':'#FFFFFF','splash_background':'#060708'},'review_notes':['Root reviewed v1 at 512/320px and requested a clearer N and narrower branch cut.','v2 removes the leaf that distorted N and narrows the negative-space branch cut.','Root approved v2 static mark for integration.'],'layers':manifest},indent=2),encoding='utf-8')
print(json.dumps({'hero':str(OUT/'Branch9_Hero.png'),'svg':str(ROOT/'Branch9.svg'),'layers':manifest},indent=2))
