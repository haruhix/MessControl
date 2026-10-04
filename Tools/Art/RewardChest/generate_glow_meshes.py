"""Small UV-mapped unlit effects, in the native OBJ importer's Z-up coordinates."""
import math
from pathlib import Path

ROOT = Path(__file__).parent


class Mesh:
    def __init__(self):
        self.vertices, self.uvs, self.faces = [], [], []

    def vertex(self, point, uv):
        self.vertices.append((point[0], -point[1], point[2]))
        self.uvs.append(uv)
        return len(self.vertices)

    def quad(self, points, uvs):
        ids = [self.vertex(p, uv) for p, uv in zip(points, uvs)]
        self.faces.extend([(ids[0], ids[1], ids[2]), (ids[0], ids[2], ids[3])])

    def write(self, name):
        lines = ['# Reward chest presentation mesh; Unreal units are centimeters.', 'o ' + name]
        lines += ['v %.6f %.6f %.6f' % p for p in self.vertices]
        lines += ['vt %.6f %.6f' % uv for uv in self.uvs]
        lines += ['f ' + ' '.join(f'{i}/{i}' for i in f) for f in self.faces]
        path = ROOT / (name + '.obj')
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        print(path, len(self.vertices), 'vertices', len(self.faces), 'triangles')


def add(p, q):
    return tuple(a+b for a, b in zip(p, q))


def scale(p, n):
    return tuple(a*n for a in p)


def rays():
    mesh = Mesh()
    # Crossed ribbons give each beam thickness from all viewing directions.
    for i in range(18):
        angle = 2*math.pi*i/18 + .09*math.sin(i*3.1)
        elevation = .32 + .5*(.5+.5*math.sin(i*2.7))
        length = 130 + 48*math.sin(i*1.9)**2
        direction = (math.cos(angle)*math.cos(elevation), math.sin(angle)*math.cos(elevation), math.sin(elevation))
        tangent = (-math.sin(angle), math.cos(angle), 0)
        normal = (-math.cos(angle)*math.sin(elevation), -math.sin(angle)*math.sin(elevation), math.cos(elevation))
        start = add((0,0,74), scale(direction, 30))
        end = add(start, scale(direction, length))
        width = 5+3*math.sin(i*2.3)**2
        for axis in (tangent, normal):
            mesh.quad([add(start,scale(axis,-2)), add(start,scale(axis,2)),
                       add(end,scale(axis,width)), add(end,scale(axis,-width))],
                      [(0,0),(1,0),(1,1),(0,1)])
    mesh.write('SM_RewardChest_Rays')


def spotlight():
    mesh = Mesh()
    # Nested open shells make a soft volume without requiring global fog.
    for layer in range(4):
        radius = 92 + layer*12
        for i in range(48):
            a, b = 2*math.pi*i/48, 2*math.pi*(i+1)/48
            mesh.quad([(radius*math.cos(a),radius*math.sin(a),10),
                       (radius*math.cos(b),radius*math.sin(b),10),
                       (5*math.cos(b),5*math.sin(b),420),
                       (5*math.cos(a),5*math.sin(a),420)],
                      [(i/48,0),((i+1)/48,0),((i+1)/48,1),(i/48,1)])
    mesh.write('SM_RewardChest_Spotlight')


def halo():
    mesh = Mesh()
    mesh.quad([(-145,-145,4),(145,-145,4),(145,145,4),(-145,145,4)],
              [(0,0),(1,0),(1,1),(0,1)])
    mesh.write('SM_RewardChest_Halo')


if __name__ == '__main__':
    rays()
    spotlight()
    halo()
