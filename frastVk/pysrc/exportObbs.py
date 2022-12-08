import sys, os, numpy as np
from .transformAuthalicToGeodetic import authalic_to_geodetic_corners, authalic_to_wgs84_pt, EarthGeodetic
from .proto import rocktree_pb2 as RT
from .utils import unit_wm_to_ecef

from argparse import ArgumentParser
from tqdm import tqdm

# Version 1 Format:
#     - Just a bunch of rows with binary data
#              - The first 26 bytes are the node's key. The left-most bytes have digits 0-8. The right-most are filled 255 to indicate invalid
#              - Next are 10 floats (3 + 3 + 4) for (pos extent quat)

# https://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
def R_to_quat(R):
    tr = np.trace(R)
    if tr > 0:
        S = np.sqrt(tr+1) * 2
        q =  np.array((
            .25*S,
            (R[2,1] - R[1,2]) / S,
            (R[0,2] - R[2,0]) / S,
            (R[1,0] - R[0,1]) / S))
    elif (R[0,0]>R[1,1]) and (R[0,0]>R[2,2]):
        S = np.sqrt(1 + R[0,0]-R[1,1]-R[2,2]) * 2
        q =  np.array((
            (R[2,1] - R[1,2]) / S,
            .25*S,
            (R[0,1] + R[1,0]) / S,
            (R[0,2] + R[2,0]) / S))
    elif R[1,1]>R[2,2]:
        S = np.sqrt(1 + R[1,1]-R[0,0]-R[2,2]) * 2
        q =  np.array((
            (R[0,2] - R[2,0]) / S,
            (R[0,1] + R[1,0]) / S,
            .25*S,
            (R[1,2] + R[2,1]) / S))
    else:
        S = np.sqrt(1 + R[2,2]-R[0,0]-R[1,1]) * 2
        q =  np.array((
            (R[1,0] - R[0,1]) / S,
            (R[0,2] + R[2,0]) / S,
            (R[1,2] + R[2,1]) / S,
            .25*S))
    return q / np.linalg.norm(q)


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

def rt_get_level_and_path_and_flags(path_and_flags):
    level = 1 + (path_and_flags & 3)
    path_and_flags >>= 2
    path = ''
    for i in range(level):
        path += chr(ord('0') + (path_and_flags & 7))
        path_and_flags >>= 3
    # while len(path) % 4 != 0: path = path + '0'
    flags = path_and_flags
    return level, path, flags

def rt_decode_obb(bites, headNodeCtr, metersPerTexel):
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

def export_rt_version1(outFp, root, transformToWGS84=True, bulkOverride=''):

    if bulkOverride:
        bulkDir = bulkOverride
    else:
        bulkDir = os.path.join(root, 'bulk')
    nodeDir = os.path.join(root, 'node')

    nodeSet = set()
    for fi in os.listdir(nodeDir):
        nodeSet.add(fi)
    print(' - Have {} nodes'.format(len(nodeSet)))

    bulks = os.listdir(bulkDir)
    print(' - Have {} bulks'.format(len(bulks)))
    nodesSeen = 0

    # for i,fi in enumerate(bulks):
    for i,fi in enumerate(tqdm(bulks)):
        # if i % 1000 == 0: print(' - bulk {} ({} / {} nodes)'.format(i, nodesSeen, len(nodeSet)))
        filename = os.path.join(bulkDir, fi)

        bulkPath = fi.split('_')[0]
        bulkPath = bulkPath.replace('root','')

        with open(filename,'rb') as fp:
            bulk = RT.BulkMetadata.FromString(fp.read())

            head_center = bulk.head_node_center
            mtt_per_level = bulk.meters_per_texel

            for i in range(len(bulk.node_metadata)):
                rlevel, rpath, flags = rt_get_level_and_path_and_flags(bulk.node_metadata[i].path_and_flags)

                path = bulkPath + rpath
                if path in nodeSet:
                    if len(bulk.node_metadata[i].oriented_bounding_box) < 15:
                        print(' - item', path+'/'+str(len(path)), 'from', filename, 'had OBB len', len(bulk.node_metadata[i].oriented_bounding_box))
                    else:
                        ctr, ext, R = rt_decode_obb(bulk.node_metadata[i].oriented_bounding_box, head_center, mtt_per_level[rlevel-1])

                        if transformToWGS84:
                            corners0 = np.array((
                                0,0,0,
                                0,0,1,
                                1,0,0,
                                0,1,0)).reshape(4,3)

                            corners = ((corners0 - .5) * 2 * ext[np.newaxis] + ctr) @ R.T
                            T = authalic_to_geodetic_corners(corners)

                            #print('R0\n', R)
                            #print('ctr0', ctr)

                            ext = ext @ T[:3,:3].T / EarthGeodetic.R1
                            ctr = authalic_to_wgs84_pt(ctr) / EarthGeodetic.R1
                            R = T[:3,:3] @ R

                        q = R_to_quat(R)
                        #print('R1\n', R)
                        #print('ctr1', ctr)

                        keybuf = np.zeros(26, dtype=np.uint8) + 255
                        for i in range(len(path)):
                            keybuf[i] = ord(path[i]) if path[i] != 255 else 0
                        # print(keybuf)


                        buf = np.zeros(3+3+4, dtype=np.float32)
                        buf[0:3]  = ctr
                        buf[3:6]  = ext
                        buf[6:10] = q
                        outFp.write(keybuf.tobytes())
                        outFp.write(buf.tobytes())
                        nodesSeen += 1


def export_frast_version1(outFp, colorPath, elevPath):
    import frastpy
    o = frastpy.DatasetReaderOptions()
    c_dset = frastpy.DatasetReader(colorPath, o)
    e_dset = frastpy.DatasetReader(elevPath, o)

    minElev0 = 0 / frastpy.WebMercatorMapScale
    maxElev0 = 200 / frastpy.WebMercatorMapScale
    elevBuf = np.zeros((8,8,2), dtype=np.uint8) # Buffer should be uint8x2
    nseen = 0

    for lvl in c_dset.getExistingLevels():
        for coord in c_dset.iterCoords(lvl):
            z,y,x = coord.z(), coord.y(), coord.x()

            ox = 2.*x / (1<<z) - 1.
            oy = 2.*y / (1<<z) - 1.
            lvlScale = 2. / (1<<z)

            if e_dset:
		        #.def("rasterIo", [](DatasetReader& dset, py::array_t<uint8_t> out, py::array_t<double> tlbrWm_) -> py::object {
                tlbr_wm = np.array((ox, oy, ox+lvlScale, oy+lvlScale), dtype=np.float64) * frastpy.WebMercatorMapScale
                # print(tlbr_wm)
                # tlbr_wm = np.array((ox, oy, ox+lvlScale, oy+lvlScale), dtype=np.float64)
                e_dset.rasterIo(elevBuf, tlbr_wm)
                elevBuf_ = elevBuf.view(np.uint16) # Actually stored as uint16
                minElev = (elevBuf_.min() / 8) / np.pi
                maxElev = (elevBuf_.max() / 8) / np.pi
            else:
                minElev, maxElev = minElev0, maxElev0

            # lvlScale = frastpy.WebMercatorMapScale
            lvlScales = np.array([lvlScale,lvlScale, maxElev-minElev])[np.newaxis]

            oz = minElev

            uwm_corners = truth_table() * lvlScales + np.array([ox,oy,oz])[np.newaxis]
            corners = np.zeros_like(uwm_corners)
            for i in range(len(uwm_corners)): corners[i] = unit_wm_to_ecef(uwm_corners[i]) / EarthGeodetic.R1

            ctr = corners.mean(0).astype(np.float32)
            extents = (corners - ctr).max(0).astype(np.float32)
            q = np.array([1,0,0,0], dtype=np.float32)

            buf = np.zeros(3+3+4,dtype=np.float32)
            buf[0:3]  = ctr
            buf[3:6]  = extents
            buf[6:10] = q

            outFp.write(np.uint64(coord.c()).tobytes())
            outFp.write(buf.tobytes())


            nseen += 1
            # if nseen&(nseen-1) == 0:
            # if nseen % 10000 == 0:
            if nseen % 1000 == 0:
                print(' - on {:6d}, at'.format(nseen), z,y,x, ctr, extents)
                # print(' - elev', minElev, maxElev)



if __name__ == '__main__':

    parser = ArgumentParser()
    parser.add_argument('--frastColor', help='color dataset, if using frast')
    parser.add_argument('--frastElev', help='elev dataset, if using frast')
    parser.add_argument('--gearthDir', help='path to google-earth root data, if using gearth')
    parser.add_argument('--gearthBulkDirOverride',
        help='optional override bulk path, useful if nodes were authalic->geodetic xformed but bulks were not',
        default='')
    args = parser.parse_args()

    indexVersionStr = 'v1'

    if args.gearthDir:
        indexFile = os.path.join(args.gearthDir, 'index.{}.bin'.format(indexVersionStr))
        with open(indexFile, 'wb') as fp: export_rt_version1(fp, args.gearthDir, bulkOverride=args.gearthBulkDirOverride)

    if args.frastColor:
        assert(args.frastElev)
        colorDir = os.path.split(args.frastColor)[0]
        indexFile = os.path.join(colorDir, 'index.{}.bin'.format(indexVersionStr))
        with open(indexFile, 'wb') as fp: export_frast_version1(fp, colorPath=args.frastColor, elevPath=args.frastElev)

