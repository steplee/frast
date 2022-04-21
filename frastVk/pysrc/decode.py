import numpy as np
import cv2

###########################################################################
#   Decode funcs
###########################################################################

def decode_verts(bites):
    xyz = np.frombuffer(bites, dtype=np.uint8).reshape(3,-1)
    xyz = xyz.cumsum(1, dtype=np.uint8)
    return xyz.T

def decode_inds(bites):
    strip_len, i = unpackVarInt(bites, 0)
    strip = np.zeros(strip_len, dtype=np.uint16)
    zeros = 0
    a, b, j = 0, 0, 0
    while i < len(bites):
        v,i = unpackVarInt(bites, i)
        strip[j] = zeros - v
        a = b
        b = strip[j]
        if v == 0: zeros += 1
        j += 1
    return strip[:j]

def unpackVarInt(b, i):
    z = b[i]
    c, d = 0, 1
    while True:
        e = b[i]
        i += 1
        c = c + (e&0x7f) * d
        d <<= 7
        if e & 0x80 == 0: return c, i

	
# bites -> uvs, uvOff, uvScale
def decode_uv(bites):
    n = (len(bites) - 2) // 4
    uvs = np.zeros((n, 2), dtype=np.uint8)
    uv_mod = 1 + np.frombuffer(bites[:4],dtype=np.uint16)
    data = np.frombuffer(bites[4:], dtype=np.uint8)

    if 0:
        uvs = np.stack((
            data[   :n  ] + (data[n*2:n*3] << 8),
            data[n*1:n*2] + (data[n*3:n*4] << 8)), -1) % uv_mod.reshape(1,2).astype(np.uint16)
        uvs = uvs.cumsum(0).astype(np.uint16)
    else:
        uvs = np.zeros((n,2),dtype=np.uint16)
        u, v = 0, 0
        for i in range(n):
            u = (u + data[i  ] + (data[i*3]<<8)) % uv_mod[0]
            v = (v + data[i*2] + (data[i*4]<<8)) % uv_mod[1]
            uvs[i] = u, v

    uvScale = 1.0 / uv_mod.astype(np.float32)
    uvOff = np.array((.5,.5),dtype=np.float32)
    return uvs, uvScale, uvOff

def decode_for_normals(bites):
    n = np.frombuffer(bites[:2], dtype=np.uint16)[0]
    s = np.frombuffer(bites[2:3], dtype=np.uint8)[0]
    data = np.frombuffer(bites[3:], dtype=np.uint8)
    out = np.zeros((n,3),dtype=np.uint8)

    def f1(v,s):
        if s <= 4:
            return (v << s) + (v & (1<<s) - 1)
        if s <= 6:
            r = 8 - s
            return (v << s) + (v << l >> r) + (v << l >> r >> r) + (v << l >> r >> r >> r)
        return -(v & 1)

    def f2(c):
        return np.clip(np.round(c), 0, 255).astype(np.uint8)

    for i in range(n):
        a,f = f1(data[i], s) / 255., f1(data[n+i], s) / 255.
        b = a
        c = f
        g = b + c
        h = b - c
        sign = 1

        if not (.5 <= g and 1.5 >= g and -.5 <= h and .5 >= h):
            sign = -1
            if .5 >= g: b,c = .5 - f, .5 - a
            else:
                if 1.5 <= g: b,c = 1.5 - f, 1.5 - a
                else:
                    if -.5 >= h: b, c = f - .5, a + .5
                    else: b,c = f + .5, a - .5
            g,h = b + c, b - c

        a = min(min(2*g-1, 3-2*g), min(2*h+1, 1-2*h)) * sign
        b,c = 2*b-1, 2*c-1
        m = 127 / np.sqrt(a*a + b*b + c*c)
        out[i,0] = f2(m*a+127)
        out[i,1] = f2(m*b+127)
        out[i,2] = f2(m*c+127)
    return out

# If @bites & @forNormals are None, must provide @n to initialize with empty normals
def decode_normal_inds(bites, forNormals, n=0):
    if (bites is not None) and (forNormals is not None):
        data = np.frombuffer(bites, dtype=np.uint8)
        n = data.size
        out = np.zeros((n,4),dtype=np.uint8)
        for i in range(n):
            j = data[i] + (data[n+i]<<8)
            out[i,0:3] = forNormals[j,0:3]
            out[i,3] = 0
    else:
        out = np.ones((n,4),dtype=np.uint8)
        out[:,3] = 0
    return out

# bytes -> (center, extent, rotation : 3x3)
def decode_obb(bites, headNodeCtr, metersPerTexel):
    assert len(bites) == 15
    data = np.frombuffer(bites,dtype=np.uint8)

    ctr = np.frombuffer(bites[:6], dtype=np.int16).astype(np.float32) * metersPerTexel + headNodeCtr
    ext = np.frombuffer(bites[6:9], dtype=np.uint8).astype(np.float32) * metersPerTexel

    euler = np.frombuffer(bites[9:], dtype=np.uint16) * (np.pi/32768, np.pi/65536, np.pi/32768)
    c0 = np.cos(euler[0])
    s0 = np.sin(euler[0])
    c1 = np.cos(euler[1])
    s1 = np.sin(euler[1])
    c2 = np.cos(euler[2])
    s2 = np.sin(euler[2])

    return ctr, ext, np.array((
        c0*c2-c1*s0*s2, c1*c0*s2+c2*s0, s2*s1,
        -c0*s2-c2*c1*s0, c0*c1*c2-s0*s2, c2*s1,
        s1*s0, -c0*s1, c1), dtype=np.float32).reshape(3,3)

def decode_obb_simple(bites, headNodeCtr, metersPerTexel):
    assert len(bites) == 15
    data = np.frombuffer(bites,dtype=np.uint8)

    ctr = np.frombuffer(bites[:6], dtype=np.int16).astype(np.float32) * metersPerTexel + headNodeCtr
    ext = np.frombuffer(bites[6:9], dtype=np.uint8).astype(np.float32) * metersPerTexel
    return ctr,ext


FORMAT_JPEG = 1
def decode_image(bites, format):
    assert format == FORMAT_JPEG, ' - Only JPEG decoding is supported!'
    return cv2.imdecode(bites, cv2.IMREAD_UNCHANGED)
