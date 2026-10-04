"""Geometry for animated, camera-facing treasure glow and attached luminous details."""
from generate_glow_meshes import Mesh


def backdrop(name, count=1):
    mesh = Mesh()
    # Full size bounds contain the shader-driven sprite animation at every phase.
    for i in range(count):
        mesh.quad([(-220, 0, -220), (220, 0, -220),
                   (220, 0, 220), (-220, 0, 220)],
                  [(i+.125, .001), (i+.875, .001),
                   (i+.875, .999), (i+.125, .999)] if count > 1 else
                  [(.001, .001), (.999, .001), (.999, .999), (.001, .999)])
    mesh.write(name)


def seam():
    mesh = Mesh()
    points = [(-51.5, -57), (-41, -69.2), (38, -69.2), (49, -57),
              (49, 57), (38, 69.2), (-41, 69.2), (-51.5, 57)]
    for a, b in zip(points, points[1:]+points[:1]):
        mesh.quad([(a[0], a[1], 66.0), (b[0], b[1], 66.0),
                   (b[0], b[1], 68.4), (a[0], a[1], 68.4)],
                  [(0, 0), (1, 0), (1, 1), (0, 1)])
    mesh.write('SM_RewardChest_SeamContour')


def keyhole():
    mesh = Mesh()
    mesh.quad([(56.3, -6.82, 44), (56.3, 7.18, 44),
               (56.3, 7.18, 66), (56.3, -6.82, 66)],
              [(0, 0), (1, 0), (1, 1), (0, 1)])
    mesh.write('SM_RewardChest_KeyLight')


if __name__ == '__main__':
    backdrop('SM_RewardChest_Radiance')
    backdrop('SM_RewardChest_MagicSparksStable', 40)
    seam()
    keyhole()
