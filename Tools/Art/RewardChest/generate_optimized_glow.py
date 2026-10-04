"""Bake expensive pixel math once, and generate compact geometry for chest VFX."""
import math
from pathlib import Path
import numpy as np
from PIL import Image
from generate_glow_meshes import Mesh
from generate_reference_glow import backdrop

ROOT = Path(__file__).parent


def smooth(a, b, x):
    t = np.clip((x-a)/(b-a), 0, 1)
    return t*t*(3-2*t)


def grid(w, h):
    return np.meshgrid((np.arange(w)+.5)/w, (np.arange(h)+.5)/h)


def save(name, mask):
    Image.fromarray(np.uint8(np.clip(mask, 0, 1)*255)).save(ROOT/(name+'.png'))


def textures():
    u, v = grid(512, 512)
    x, y = (u-.5)*2, (v-.5)*2
    r, angle = np.hypot(x, y), np.arctan2(y, x)
    rays = np.zeros_like(r)
    for layer, count in [(0, 29), (1, 41)]:
        a = angle/(2*math.pi)
        cell = np.floor(np.mod(a, 1)*count)
        d = np.mod(a*count, 1)-.5
        h = np.mod(np.sin(cell*127.1+layer*311.7)*43758.5453, 1)
        extent, width = .5+h*.49, .065+h*.12
        shaft, spine = np.exp(-d*d/(width*width)), np.exp(-d*d/(.018*.018))
        rays += (shaft*.95+spine*.16)*np.clip(1-r/extent, 0, 1)**1.35*smooth(.04, .16, r)
    corona = np.exp(-r*r*12)*.9 + np.exp(-r*r*3.8)*.11
    # Square-root encoding retains smooth dark gradients in a compact R8 texture.
    save('T_RewardChest_RadianceSoftMask', np.sqrt(np.clip((rays+corona)*(1-smooth(.84, 1, r))/1.25, 0, 1)))

    u, v = grid(128, 256)
    width = .015+.48*v
    side = np.exp(-((u-.5)/np.maximum(width*.43, .01))**2)
    ends = smooth(.02, .16, v)*(1-smooth(.84, 1, v))
    save('T_RewardChest_BeamMask', side*ends)

    u, v = grid(128, 128)
    r = np.hypot((u-.5)*2, (v-.5)*2)
    save('T_RewardChest_FloorMask', (np.exp(-((r-.6)*7)**2)*.38+np.exp(-r*r*7)*.4)*(1-smooth(.78, 1, r)))

    u, v = grid(64, 64)
    p, q = np.abs((u-.5)*2), np.abs((v-.5)*2)
    disc = np.exp(-(p*p+q*q)*5.8)
    star = np.clip(1-np.sqrt(p)-np.sqrt(q), 0, 1)**.7+np.exp(-(p*p+q*q)*14)*.3
    save('T_RewardChest_SparkAtlas', np.concatenate([disc, star], axis=1))
    u, v = grid(64, 16)
    save('T_RewardChest_SeamMask', np.exp(-((v-.5)*7)**2))


def geometry():
    disk = Mesh()
    center = disk.vertex((0, 0, 0), (.5, .5))
    ids = []
    for i in range(32):
        a = 2*math.pi*i/32
        x, z = math.cos(a), math.sin(a)
        ids.append(disk.vertex((220*x, 0, 220*z), (.5+x*.5, .5+z*.5)))
    for i in range(32):
        disk.faces.append((center, ids[i], ids[(i+1)%32]))
    disk.write('SM_RewardChest_RadianceDisk')
    beam = Mesh()
    ids = [beam.vertex((-105, 0, 10), (0, 0)),
           beam.vertex((105, 0, 10), (1, 0)),
           beam.vertex((0, 0, 420), (.5, 1))]
    beam.faces.append(tuple(ids))
    beam.write('SM_RewardChest_SingleBeam')
    backdrop('SM_RewardChest_SparksLite', 20)


if __name__ == '__main__':
    textures()
    geometry()
