"""Bake seamless liquid normals and packed caustics/foam fields, deterministically."""
from pathlib import Path
import numpy as np
from PIL import Image

out=Path(__file__).resolve().parents[2]/'ArtSource/Fluids'
out.mkdir(parents=True,exist_ok=True)
n=512
y,x=np.mgrid[:n,:n].astype(np.float32)/n
rng=np.random.default_rng(37021)
frequency=np.fft.fftfreq(n)*n
kx,ky=np.meshgrid(frequency,frequency)
radial=np.sqrt(kx*kx+ky*ky)
def field(seed,scale):
    noise=np.random.default_rng(seed).normal(size=(n,n))
    spectrum=np.fft.fft2(noise)*np.exp(-.5*(radial/scale)**2)
    h=np.fft.ifft2(spectrum).real
    return (h-h.mean())/h.std()
h=field(108,8)+field(47,24)*.24+field(209,58)*.035
dx=(np.roll(h,-1,axis=1)-np.roll(h,1,axis=1))*.5
dy=(np.roll(h,-1,axis=0)-np.roll(h,1,axis=0))*.5
slope=np.sqrt(np.mean(dx*dx+dy*dy))
normals=np.stack((-dx/slope*.24,-dy/slope*.24,np.ones_like(dx)),axis=2)
normals/=np.linalg.norm(normals,axis=2,keepdims=True)
Image.fromarray(np.uint8(np.clip(normals*.5+.5,0,1)*255)).save(out/'T_LiquidRipples_N.png')

# Wrapped jittered Voronoi cells. Warp also repeats at every tile boundary.
cells=8
points=rng.uniform(.15,.85,size=(cells,cells,2)).astype(np.float32)
qx=x*cells+.16*np.sin(2*np.pi*y*2)+.08*np.sin(2*np.pi*(x+y)*3)
qy=y*cells+.17*np.sin(2*np.pi*x*2)+.07*np.cos(2*np.pi*(x-y)*3)
cx=np.floor(qx).astype(int); cy=np.floor(qy).astype(int)
f1=np.full((n,n),1e6); f2=f1.copy()
for oy in (-1,0,1):
    for ox in (-1,0,1):
        jitter=points[(cy+oy)%cells,(cx+ox)%cells]
        d=(cx+ox+jitter[:,:,0]-qx)**2+(cy+oy+jitter[:,:,1]-qy)**2
        f2=np.minimum(f2,np.maximum(f1,d)); f1=np.minimum(f1,d)
edge=np.maximum(0,np.sqrt(f2)-np.sqrt(f1))
caustic=np.exp(-(edge/.065)**2)*.84+np.exp(-(edge/.15)**2)*.16
cloud=np.clip(.5+field(57,6)*.16+field(17,17)*.035,0,1)
detail=np.clip(.5+field(213,20)*.16,0,1)
foam=np.clip((.23-edge)/.23,0,1)*cloud
packed=np.stack((caustic,cloud,detail,foam),axis=2)
Image.fromarray(np.uint8(np.clip(packed,0,1)*255)).save(out/'T_LiquidDetail_M.png')
print('Liquid textures: seamless 512px normals + packed caustics, foam and flow detail')
