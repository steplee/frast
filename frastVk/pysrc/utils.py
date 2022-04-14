import numpy as np, os, sys, cv2

class Earth:
    R1         = 6378137.0
    R2         = 6356752.314245179
    a          = 6378137.0
    b          = a * (R2 / R1)
    b2_over_a2 = (b / a) * (b / a)
    e2         = 1 - b2_over_a2

    # Normalized scenario, where semi-major axis is unit and semi-minor is .997...
    na = 1
    nb          = na * (R2 / R1)
    nb2_over_a2 = (nb / na) * (nb / na)
    ne2         = 1 - nb2_over_a2


def geodetic_to_ecef(lng,lat, alt):

    cos_phi, cos_lam = np.cos(np.deg2rad(lat)), np.cos(np.deg2rad(lng))
    sin_phi, sin_lam = np.sin(np.deg2rad(lat)), np.sin(np.deg2rad(lng))
    n_phi = Earth.a / np.sqrt(1 - Earth.e2 * sin_phi * sin_phi)

    return np.array(((n_phi + alt) * cos_phi * cos_lam, \
           (n_phi + alt) * cos_phi * sin_lam, \
           (Earth.b2_over_a2 * n_phi + alt) * sin_phi))

def ecef_to_geodetic(x,y,z):
    z,y,x = z/Earth.R1,y/Earth.R1,x/Earth.R1

    lng = np.arctan2(y,x)

    k = 1 / (1-Earth.ne2)
    z2 = z*z
    p2 = x*x + y*y
    p = np.sqrt(p2)
    for j in range(2):
        c_i = ((((1-Earth.ne2)*z2) * (k*k) + p2) ** 1.5) / Earth.ne2
        k = (c_i + (1-Earth.ne2) * z2 * (k**3)) / (c_i-p2)
    lat = np.arctan2(k*z,p)

    rn = Earth.na / np.sqrt(1-Earth.ne2*(np.sin(lat)) ** 2)
    sinabslat, coslat = np.sin(abs(lat)), np.cos(lat)
    alt = (abs(z) + p - rn * (coslat + (1-Earth.ne2) * sinabslat)) / (coslat + sinabslat)

    return np.array((np.rad2deg(lng), np.rad2deg(lat), alt*Earth.R1))

def box_contains(tl, br, pt):
    x = pt - tl
    return (x < br).all()
def box_xsects(tl1,br1, tl2,br2):
    assert (tl1<br1).all()
    assert (tl2<br2).all()
    return (tl1 <= br2).all() and (br1 >= tl2).all()



######################
# Quaternion stuff
######################

def normalize1(x): return x / np.linalg.norm(x)

# https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
def quat_to_matrix(p):
    w,x,y,z = p
    return np.array((
        1 - 2 * (y*y + z*z),
        2 * (x*y - w*z),
        2 * (q*y + x*z),
        2 * (x*y + w*z),
        1 - 2 * (x*x + z*z),
        2 * (y*z - w*x),
        2 * (x*z - w*y),
        2 * (w*x + y*z),
        1 - 2 * (x*x + y*y))).reshape(3,3)

def quat_mul(p,q):
    r = np.array(4,dtype=np.float64)
    r[0] = p[0]*q[0] - p.dot(q)
    r[1:] = p[0]*q[1:] + q[0]*p[1:] - np.cross(p[1:],q[1:])
    return r

def quat_exp(u):
    angle = np.linalg.norm(u)
    v = u / angle
    return np.array((np.cos(angle/2.0), np.sin(angle/2.0)*v))

def quat_log(q):
    angle = np.arccos(q[0])
    # TODO:
    if angle < 1e-6:
        pass
    else:
        pass

# https://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
def R_to_quat(R):
    tr = np.trace(R)
    if tr > 0:
        S = np.sqrt(tr+1)*2
        return np.array((.25*S,
            (R[2,1]-R[1,2]) / S,
            (R[0,2]-R[2,0]) / S,
            (R[1,0]-R[0,1]) / S))
    elif R[0,0]>R[1,1] and R[0,0] > R[2,2]:
        S = np.sqrt(1+R[0,0]-R[1,1]-R[2,2]) * 2
        return np.array((
            (R[2,1]-R[1,2]) / S,
            .25 * S,
            (R[0,1]+R[1,0]) / S,
            (R[0,2]+R[2,0]) / S))
    elif R[1,1] > R[2,2]:
        S = np.sqrt(1+R[1,1]-R[0,0]-R[2,2])*2
        return np.array((
            (R[0,2]-R[2,0]) / S,
            (R[0,1]+R[1,0]) / S,
            .25 * S,
            (R[1,2] + R[2,1]) / S))
    else:
        S = np.sqrt(1+R[2,2]-R[0,0]-R[1,1])*2
        return np.array((
            (R[1,0]-R[0,1]) / S,
            (R[0,2]+R[2,0]) / S,
            (R[1,2]+R[2,1]) / S,
            .25 * S))


def generate_random_rvec(amplitude, axisWeight=(1,1,5)):
    axis = (np.random.random(3) - .5) * mult
    axis = (axis * axisWeight)
    axis = axis / np.linalg.normalize(axis)
    angle = (np.random.random() - .5) * amplitude * 2.
    return axis, angle

# Take pos, quat
# Return pos, quat, logQuat (which is the box-minus of the input vs output)
def perturb_pose(p,q, posAmp=10, oriAmp=.1):
    p1 = p + np.random.random(3) * posAmp / Earth.R1

    rvec = generate_random_rvec(oriAmp)
    q1 = quat_mul(p, quat_exp(rvec))

    return p1, q1, rvec

def lookAtR(eye, ctr, up):
    f = normalize1(ctr - eye)
    r = normalize1(np.cross(f, up))
    u = normalize1(np.cross(f,r))
    return np.stack((r,u,f)).reshape(3,3)
