import numpy as np, os, sys, cv2

######################
# Coordinate stuff
######################

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
'''

# MISNOMERS: RT does not use geodetic, but authalic coordinates!
class Earth:
    R = 6371010.0
    a = b = R
    na = nb = 1
def geodetic_to_ecef(lng,lat, alt):
    # print(lng,lat,alt)
    cos_phi, cos_lam = np.cos(np.deg2rad(lat)), np.cos(np.deg2rad(lng))
    sin_phi, sin_lam = np.sin(np.deg2rad(lat)), np.sin(np.deg2rad(lng))
    n_phi = 0
    return (Earth.R + alt) * np.array((cos_phi * cos_lam, \
           cos_phi * sin_lam, \
           sin_phi))

def ecef_to_geodetic(x,y,z):
    z,y,x = z/Earth.R,y/Earth.R,x/Earth.R

    lng = np.arctan2(y,x)
    p = np.sqrt(x*x+y*y)
    lat = np.arctan2(z,p)

    # rn = Earth.na / np.sqrt(1-Earth.ne2*(np.sin(lat)) ** 2)
    # sinabslat, coslat = np.sin(abs(lat)), np.cos(lat)
    # alt = (abs(z) + p - rn * (coslat + (1-Earth.ne2) * sinabslat)) / (coslat + sinabslat)
    alt = np.linalg.norm((x,y,z)) - 1.0
    # print(lng,lat,np.linalg.norm((x,y,z)))

    return np.array((np.rad2deg(lng), np.rad2deg(lat), alt*Earth.R))
'''

one_div_two_pi = 1. / (2*np.pi)
def geodetic_to_unit_wm(ll):
    return torch.stack((
        (ll[:,0] + np.pi).mul_(one_div_two_pi),
        (np.pi - (np.pi/4 + ll[:,1]*.5).tan_().log_()).mul_(one_div_two_pi)
        #one_div_two_pi * (np.pi - torch.log(torch.tan(np.pi/4 + ll[:,1]*.5)))
    ), -1) * 2 - 1

'''
void unit_wm_to_geodetic(double* out, int n, const double* xyz) {
    for (int i = 0; i < n; i++) {
        out[i * 3 + 0] = xyz[i * 3 + 0] * M_PI;
        out[i * 3 + 1] = std::atan(std::exp(xyz[i * 3 + 1] * M_PI)) * 2 - M_PI / 2;
        out[i * 3 + 2] = xyz[i * 3 + 2] * M_PI;
    }
}

void unit_wm_to_ecef(double* out, int n, const double* xyz) {
    // OKAY: both unit_wm_to_geodetic and geodetic_to_ecef can operate in place.
    unit_wm_to_geodetic(out, n, xyz);
    geodetic_to_ecef(out, n, out);
}
'''

def unit_wm_to_geodetic(xyz):
    x,y,z = xyz
    return np.array((x*180, np.rad2deg(np.arctan(np.exp(y*np.pi)) * 2 - np.pi/2.), z*np.pi))
def unit_wm_to_ecef(xyz):
    xyz = unit_wm_to_geodetic(xyz)
    return geodetic_to_ecef(*xyz)

truth_table_ = np.array((
    0,0,0,
    0,0,1,
    0,1,0,
    0,1,1,
    1,0,0,
    1,0,1,
    1,1,0,
    1,1,1)).reshape(-1,3)[:, [2,1,0]]
def truth_table():
    global truth_table_
    return truth_table_


######################
# Intersection stuff
######################

def box_contains(tl, br, pt):
    x = pt - tl
    return (x < br).all()
def box_xsects(tl1,br1, tl2,br2):
    assert (tl1<br1).all()
    assert (tl2<br2).all()
    return (tl1 <= br2).all() and (br1 >= tl2).all()

def box_xsects_rotated(box1, box2):
    # faces1 = box1[[0,1,2, 0,1,4, 1,3,5, 2,3,6, 0,2,4, 4,5,6]].reshape(6,3,3)
    # faces2 = box2[[0,1,2, 0,1,4, 1,3,5, 2,3,6, 0,2,4, 4,5,6]].reshape(6,3,3)
    # Three faces have same normals, due to symmetry, and so can be avoided
    faces1 = box1[[0,1,2, 0,1,4, 1,3,5]].reshape(3,3,3)
    faces2 = box2[[0,1,2, 0,1,4, 1,3,5]].reshape(3,3,3)

    axes1 = np.cross(faces1[:,0] - faces1[:,1], faces1[:,2] - faces1[:,1])
    bnds11 = (box1[np.newaxis] * axes1[:,np.newaxis]).sum(-1)
    bnds12 = (box2[np.newaxis] * axes1[:,np.newaxis]).sum(-1)

    # If there is no overlap in any axes, there is no collision.
    l1,r1 = bnds11.min(1), bnds11.max(1)
    l2,r2 = bnds12.min(1), bnds12.max(1)
    if (r1 < l2).any() or (l1 > r2).any(): return False

    axes2 = np.cross(faces2[:,0] - faces2[:,1], faces2[:,2] - faces2[:,1])
    bnds21 = (box1[np.newaxis] * axes2[:,np.newaxis]).sum(-1)
    bnds22 = (box2[np.newaxis] * axes2[:,np.newaxis]).sum(-1)

    l1,r1 = bnds21.min(1), bnds21.max(1)
    l2,r2 = bnds22.min(1), bnds22.max(1)
    if (r1 < l2).any() or (l1 > r2).any(): return False

    return True




######################
# Quaternion stuff
######################

def normalize1(x): return x / np.linalg.norm(x)

def quat_inv(p):
    return p * (-1,1,1,1)

# https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
def quat_to_matrix(p):
    w,x,y,z = p
    return np.array((
        1 - 2 * (y*y + z*z),
        2 * (x*y - w*z),
        2 * (w*y + x*z),
        2 * (x*y + w*z),
        1 - 2 * (x*x + z*z),
        2 * (y*z - w*x),
        2 * (x*z - w*y),
        2 * (w*x + y*z),
        1 - 2 * (x*x + y*y))).reshape(3,3)

def quat_mul(p,q):
    r = np.empty((4,),dtype=np.float64)
    r[0] = p[0]*q[0] - p[1:].dot(q[1:])
    r[1:] = p[0]*q[1:] + q[0]*p[1:] - np.cross(p[1:],q[1:])
    return r

def quat_exp(u):
    angle = np.linalg.norm(u)
    v = u / angle
    return np.array((np.cos(angle/2.0), *np.sin(angle/2.0)*v))

def quat_log(q):
    assert(False)
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
    axis = (np.random.random(3) - .5) * amplitude
    axis = (axis * axisWeight)
    axis = axis / np.linalg.norm(axis)
    angle = (np.random.random() - .5) * amplitude * 2.
    return axis * angle

# Take pos, quat
# Return pos, quat, logQuat (which is the box-minus of the input vs output)
def perturb_pose(p,q, posAmp=10, oriAmp=.1):
    p1 = p + np.random.random(3) * posAmp / Earth.R1

    rvec = generate_random_rvec(oriAmp)
    q1 = quat_mul(p, quat_exp(rvec))

    return p1, q1, rvec

def lookAtR(eye, ctr, up):
    # f = normalize1(ctr - eye)
    f = normalize1(eye - ctr)
    r = normalize1(np.cross(up, f))
    u = normalize1(np.cross(f,r))
    return np.stack((r,u,f)).reshape(3,3).T


def quat_to_z_axis(q):
    w,x,y,z = q
    return np.array((
        2 * (x*z - w*y),
        2 * (w*x + y*z),
        1 - 2 * (x*x + y*y)))
def quat_to_y_axis(q):
    w,x,y,z = q
    return np.array((
        2 * (x*y + w*z),
        1 - 2 * (x*x + z*z),
        2 * (y*z - w*x)))

# I tried https://www.euclideanspace.com/maths/algebra/vectors/lookat/index.htm,
# but it did not work.
# Instead I have a two-step process like theres, but the second (rotate-y-up) step is done local to the first,
# rather than in the outer coordsys.
# TODO: I think this will fail at the equator and/or poles.
def lookAtQ(eye, ctr, up):
    z0 = np.array((0,0,1))
    f = (ctr - eye) # Don't need to normalize these, its done later.
    r = (np.cross(f, up))
    u = normalize1(np.cross(f,r))

    axis1 = -normalize1(np.cross(z0,f))
    angle1 = np.arccos(np.dot(f,z0))
    q1 = quat_exp(axis1*angle1)

    new_y = quat_to_y_axis(q1) # We need just the Y axis of the transformed space.
    angle2 = -np.arccos(np.dot(new_y, u))
    axis2 = np.array((0,0,1))
    q2 = quat_exp(axis2*angle2)


    q = normalize1(quat_mul(q1,q2))
    return q


'''
def test_angle_axis():
    z0 = np.array((0,0,1))
    p = normalize1(np.array((.5,.5,.4)))

    lR = lookAtR(p, np.zeros(3), np.array((0,0,1)))
    print(' - lookAt R:\n', lR)

    axis1 = -normalize1(np.cross(z0,-p))
    angle1 = np.arccos(np.dot(-p,z0))
    # angle = np.deg2rad(90+45+45+45+45)

    q1 = quat_exp(axis1*angle1)


    up = lR[1]
    # up = z0

    axis2 = -p
    lR1 = quat_to_matrix(q1)
    angle2 = -np.arccos(np.dot(lR1[1], up))
    axis2 = np.array((0,0,1))
    q2 = quat_exp(axis2*angle2)


    q = quat_mul(q1,q2)
    # q = quat_mul(q2,q1)


    print(' - dq', quat_mul((R_to_quat(lR)), quat_inv(q1)))
    print(' - dq', quat_mul(quat_inv(R_to_quat(lR)), (q1)))
    print(' - dq', quat_mul(quat_inv(q1), (R_to_quat(lR))))
    print(' - dq', quat_mul(q1, quat_inv(R_to_quat(lR))))
    # print(' - cycle', lR - quat_to_matrix(R_to_quat(lR)))

    R = quat_to_matrix(q)
    print(' - lookAt R{q1}:\n', quat_to_matrix(q1))
    print(' - lookAt R{q}:\n', R)
    print(' - q1', q1)
    print(' - q2', q2)
    print(' - q', q)
    print(' - axis1', axis1)
    print(' - angle1', np.rad2deg(angle1))
    print(' - axis2', axis2)
    print(' - angle2', np.rad2deg(angle2))
    print(' - p:', p)
'''
