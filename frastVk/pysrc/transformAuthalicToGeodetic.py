import os, sys, numpy as np

class EarthGeodetic:
    R1         = 6378137.0
    R2         = 6356752.314245179
    a          = 1
    b          = a * (R2 / R1)
    b2_over_a2 = (b / a) * (b / a)
    e2         = 1 - b2_over_a2

    # Normalized scenario, where semi-major axis is unit and semi-minor is .997...
    na = 1
    nb          = na * (R2 / R1)
    nb2_over_a2 = (nb / na) * (nb / na)
    ne2         = 1 - nb2_over_a2
class EarthAuthalic:
    R = 6371010.0
    a = b = R
    na = nb = 1

def authalic_to_wgs84_pt(a):
    aa = a / EarthAuthalic.R
    x,y,z = aa
    lng = np.arctan2(y,x)
    p = np.sqrt(x*x+y*y)
    lat = np.arctan2(z,p)
    alt = np.linalg.norm(aa) - 1.0

    cos_phi, cos_lamb = np.cos(lat), np.cos(lng)
    sin_phi, sin_lamb = np.sin(lat), np.sin(lng)
    n_phi = EarthGeodetic.a / np.sqrt(1 - EarthGeodetic.e2 * sin_phi * sin_phi)
    # print(lng,lat,alt, n_phi)

    return EarthGeodetic.R1 * np.array((
        (n_phi + alt) * cos_phi * cos_lamb,
        (n_phi + alt) * cos_phi * sin_lamb,
        (EarthGeodetic.b2_over_a2 * n_phi + alt) * sin_phi))


# Return the transformation from vertices to WGS84 ECEF 'globe' coordinates, given
# the matrix that takes vertices to Authalic 'globe' coords.
def authalic_to_geodetic_tile(T0):
    pts = np.array((
        0,0,0,1,
        0,0,255,1,
        255,0,0,1,
        0,255,0,1.)).reshape(4,4) @ T0.T
    pts = pts[:, :3] / pts[:, 3:]
    pts2 = np.stack([authalic_to_wgs84_pt(p) for p in pts])

    A = np.zeros((12,12),dtype=np.float64)
    b = np.zeros(12, dtype=np.float64)
    for i in range(4):
        A[i*3+0, 0:3] = pts[i]
        A[i*3+1, 3:6] = pts[i]
        A[i*3+2, 6:9] = pts[i]
        A[i*3:(i+1)*3, -3:] = np.eye(3)
        b[i*3:(i+1)*3] = pts2[i]

    t1 = np.linalg.solve(A, b)
    T1 = np.zeros((4,4),dtype=np.float64)
    T1[0, :3] = t1[0*3:1*3]
    T1[1, :3] = t1[1*3:2*3]
    T1[2, :3] = t1[2*3:3*3]
    T1[:3,3] = t1[9:]
    T1[3] = 0,0,0,1

    return T1 @ T0

def authalic_to_geodetic_corners(pts):
    pts2 = np.stack([authalic_to_wgs84_pt(p) for p in pts])

    A = np.zeros((12,12),dtype=np.float64)
    b = np.zeros(12, dtype=np.float64)
    for i in range(4):
        A[i*3+0, 0:3] = pts[i]
        A[i*3+1, 3:6] = pts[i]
        A[i*3+2, 6:9] = pts[i]
        A[i*3:(i+1)*3, -3:] = np.eye(3)
        b[i*3:(i+1)*3] = pts2[i]

    t1 = np.linalg.solve(A, b)
    T1 = np.zeros((4,4),dtype=np.float64)
    T1[0, :3] = t1[0*3:1*3]
    T1[1, :3] = t1[1*3:2*3]
    T1[2, :3] = t1[2*3:3*3]
    T1[:3,3] = t1[9:]
    T1[3] = 0,0,0,1

    return T1
