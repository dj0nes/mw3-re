"""What the model-draw tests share (MW3's test_modeldraw.py, PM's
test_pm_modeldraw.py): floats, the comparison of one float, and random views.
"""
import math
import os
import struct


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def fbits(*xs):
    return struct.pack(f'<{len(xs)}f', *xs)


class Stats:
    def __init__(self):
        self.floats = self.exact = 0
        self.worst = 0.0
        self.where = {}


def same_float(a, b, stats, tol, where=None, pixels=0.0):
    stats.floats += 1
    if a == b or (math.isnan(a) and math.isnan(b)):
        stats.exact += 1
        return True
    stats.where[where] = stats.where.get(where, 0) + 1
    if math.isinf(a) or math.isinf(b) or math.isnan(a) or math.isnan(b):
        return False
    d = abs(a - b) / max(1.0, abs(a), abs(b))
    if abs(a - b) <= pixels:
        return True
    stats.worst = max(stats.worst, d)
    return d <= tol


# --- Scenarios ---
def mat_mul(a, b):
    """3x4 row-vector product a * b (a applied first)."""
    r = [0.0] * 12
    for row in range(3):
        for j in range(3):
            r[3 * row + j] = sum(a[3 * row + k] * b[3 * k + j] for k in range(3))
    for j in range(3):
        r[9 + j] = sum(a[9 + k] * b[3 * k + j] for k in range(3)) + b[9 + j]
    return r


def rotation(rng):
    a, b, c = (rng.uniform(-math.pi, math.pi) for _ in range(3))
    rx = [1, 0, 0, 0, math.cos(a), math.sin(a), 0, -math.sin(a), math.cos(a), 0, 0, 0]
    ry = [math.cos(b), 0, -math.sin(b), 0, 1, 0, math.sin(b), 0, math.cos(b), 0, 0, 0]
    rz = [math.cos(c), math.sin(c), 0, -math.sin(c), math.cos(c), 0, 0, 0, 1, 0, 0, 0]
    return mat_mul(mat_mul(rz, rx), ry)


def norm(v):
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / n for x in v]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def look_at(pos, target, rng):
    z = norm([p - t for p, t in zip(pos, target)])  # the camera looks along its -z
    up = [0, 1, 0] if abs(z[1]) < 0.95 else [1, 0, 0]
    if rng.random() < 0.2:
        up = norm([rng.uniform(-1, 1) for _ in range(3)])
    x = norm(cross(up, z))
    y = cross(z, x)
    return x + y + z + list(pos)


def clip_struct(x, y, w, h, near, far):
    """0x7402f8 as 0x526e10 fills it with a device, then the point rect."""
    xw, yh = f32(x + w), f32(y + h)
    right, bottom = f32(xw + 0.001), f32(yh + 0.001)
    c = struct.pack('<I', 0) + fbits(x, y, near, right, bottom, far, right, bottom)
    return c + fbits(x, y, f32(xw - 0.001), f32(yh - 0.001))


def scenario(rng, centre, radius):
    """A world matrix, camera, projection, clip structs, mask and state for one draw."""
    if rng.random() < 0.3:
        world = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]
    else:
        world = rotation(rng)
        s = rng.choice([1.0, 1.0, rng.uniform(0.3, 3.0)])
        world = [v * s for v in world[:9]] + [rng.uniform(-300, 300) for _ in range(3)]
    cw = [sum(centre[k] * world[3 * k + j] for k in range(3)) + world[9 + j] for j in range(3)]
    r = max(radius, 0.5) * (math.sqrt(sum(world[k] ** 2 for k in range(3))) or 1.0)
    f = rng.choice([0.0, 0.1, 0.3, 0.7, 1.0, 1.5, 2.5, 5.0, 12.0, 60.0])
    d = norm([rng.uniform(-1, 1) for _ in range(3)])
    pos = [c + r * f * k for c, k in zip(cw, d)]
    target = [c + r * rng.uniform(-0.6, 0.6) for c in cw]
    if f == 0.0:
        target = [p + rng.uniform(-1, 1) for p in pos]
    cam = look_at(pos, target, rng)
    near = rng.choice([0.1, 0.1, 0.1, 1.0, 5.0, max(0.1, r * f * 0.8)])
    far = rng.choice([800.0, 800.0, max(near + 1, r * f), max(near + 1, r * f * 1.3)])
    x, y, w, h = rng.choice([(0, 0, 640, 480), (0, 0, 640, 480), (0, 0, 800, 600), (40, 30, 320, 200)])
    cot_h, cot_v = rng.choice([(1.19175, 1.73205), (1.19175, 1.73205), (rng.uniform(0.5, 3), rng.uniform(0.5, 3))])
    proj = [x, y, w * 0.5, h * 0.5, cot_h, cot_v, near, far]
    clip = clip_struct(x, y, w, h, near, far)
    if rng.random() < 0.15:
        sx, sy = rng.uniform(0.2, 1), rng.uniform(0.2, 1)
        l, t = rng.uniform(-50, 300), rng.uniform(-50, 200)
        r, b = rng.uniform(l + 10, 700), rng.uniform(t + 10, 520)
        second = (struct.pack('<I', rng.choice([0, 0xf, 0xf, rng.randrange(16)])) + fbits(l, t, 0, r, b, 0)
                  + fbits(0, r, b)
                  + fbits(sx, sy, rng.uniform(-50, 50), rng.uniform(-50, 50)))
        second_on = 1
    else:
        second, second_on = bytes(0x34), 0
    mask = rng.choice([0, 0x3f, 0x3f, 0x30, 0x0f, 0x10, 0x20, rng.randrange(64), 0x2f, 0x1f])
    opacity = rng.choice([1.0, 1.0, 1.0, 0.5, 0.0])
    fog_on = rng.random() < 0.4
    dist = r * max(f, 0.1)
    fnear, ffar = rng.choice([(32.0, 750.0), (dist * 0.3, dist * 2), (0.0, dist + r)])
    alt_low = cw[1] + rng.uniform(-1, 0.5) * r
    alt_high = alt_low + rng.uniform(0.1, 2) * r
    fog = struct.pack('<ii', int(fog_on), 1) + fbits(0.8, 0.5, 0.2, fnear, fnear * fnear, ffar, ffar * ffar,
                                                       1 / (ffar - fnear) if ffar != fnear else 0, alt_high,
                                                       alt_low, 1 / (alt_high - alt_low), 2.0)
    st = [opacity, rng.random() < 0.1, rng.random() < 0.5, second_on, 0.02,
          0, rng.choice([0.0, 0.016, 0.05, 0.3]), rng.random() < float(os.environ.get("MW3_LIGHTS", "0.4")), rng.uniform(-4, 4), rng.random() < 0.2]
    return world, cam, proj, clip, second, mask, st, fog
