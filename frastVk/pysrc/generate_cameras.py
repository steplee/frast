import numpy as np, os, sys, cv2, random, json
from argparse import ArgumentParser
from multiprocessing import Pool

sys.path.append(os.path.join(os.getcwd(),'pysrc'))
import proto.rocktree_pb2 as RT
from utils import *
from decode import *

def add_row(a):
    return np.vstack((a,np.array((0,0,0,1),dtype=a.dtype)))

class PoseSet:
    def __init__(self):
        self.base = None
        self.perturbedPoses = []
        self.logDiffs = []
        self.cameraIntrin = None # w h fx fy cx cy
    def pre_serialize(self):
        # return json.dumps({
        return ({
            'base': self.base.ravel().tolist(),
            'perturbed': [a.ravel().tolist() for a in self.perturbedPoses],
            'logDiffs': [a.tolist() for a in self.logDiffs],
            'camera': self.cameraIntrin.tolist() if isinstance(self.cameraIntrin,np.ndarray) else self.cameraIntrin
            })

class TileData:
    def __init__(self, nd):
        self.nd = nd

        mesh = nd.meshes[0]
        verts = decode_verts(mesh.vertices)
        M = np.array((nd.matrix_globe_from_mesh)).reshape(4,4).T
        verts = verts.astype(np.float64) @ M[:3,:3].T + M[:3,3]

        # self.middle_ = M[:3,:3].T @ (127.5,127.5,127.5) + M[:3,3]
        self.middle_ = verts.mean(0)

        self.LTP_ = lookAtR(self.middle_, np.zeros(3), np.array((0,0,1.)))
        print(' - tile middle', self.middle_)
        # print(' - tile LTP matrix:\n', self.LTP_)

        verts_ltp = verts @ self.LTP_.T
        verts_ltp_cntrd = (verts - self.middle_) @ self.LTP_.T
        # verts_ltp_cntrd = verts_ltp - verts_ltp.mean(0,keepdims=True)
        # verts_ltp_cntrd = verts_ltp - self.middle_
        # print(' - verts_ltp:\n',verts_ltp)
        # print(' - verts_ltp_cntrd:\n',verts_ltp_cntrd)
        print(' - verts_ltp_cntrd size',verts_ltp_cntrd.max(0) - verts_ltp_cntrd.min(0))
        self.extent_ = (verts_ltp[:2].max(0) - verts_ltp[:2].min(0)).max() * .707
        extent messed up

        # some_geodetics = np.stack((geodetic_to_ecef(*c) for c in verts[::4]))
        # self.meanAlt_ = some_geodetics[:,2].mean()
        self.meanAlt_ = (np.linalg.norm(verts[::4], axis=1) - Earth.R).mean()

    def middle(self): return self.middle_
    def extent(self): return self.extent_
    def meanAlt(self): return self.meanAlt_
    def ltp(self): return self.LTP_

class Generator():
    def __init__(self, args):

        nodeFiles = []
        print(' - populating nodeFiles')
        for f in os.listdir(os.path.join(args.srcDir, 'node')):
            nodeFiles.append(os.path.join(args.srcDir,'node',f))

        lvlsByName = {}
        print(' - finding lvlsByName')
        for listFile in os.listdir(os.path.join(args.srcDir, 'list')):
            lvlsByName[listFile] = {}
            with open(os.path.join(args.srcDir,'list', listFile),'r') as fp:
                nodes = fp.read().split('\n')
            for node in nodes:
                lvl = len(node)
                if lvl > args.minLvl and lvl <= args.maxLvl:
                    if lvl not in lvlsByName[listFile]:
                        lvlsByName[listFile][lvl] = [node]
                    else:
                        lvlsByName[listFile][lvl].append(node)


        # Erase bad levels, then bad datasets.
        print(' - erasing bad levels and datasets')
        lvlsByName_ = {}
        for name,lvls in lvlsByName.items():
            all_bad = len(lvls) == 0 or all([len(arr)==0 for lvl,arr in lvls.items()])
            if not all_bad:
                lvlsByName_[name] = {}
                for lvl,arr in lvls.items():
                    if len(arr) > 0: lvlsByName_[name][lvl] = arr
        lvlsByName = lvlsByName_

        print(' - Have')
        for name,lvls in lvlsByName.items():
            print('\t -',name)
            for lvl,arr in sorted(lvls.items(),key=lambda x:x[0]):
                print('\t\t lvl {}: {} nodes'.format(lvl,len(arr)))

        self.nodeFiles, self.lvlsByName = nodeFiles, lvlsByName
        self.args = args
        self.wh = args.wh

    def generate_one(self):
        # Pick a dset, level, and tile
        # Get the corresponding ancestor tile in the altitudeLevel
        # Sample reasonable intrin, AGL altitude, and orientation (check bounds)
        # Compute initial pose
        # Generate M perturbed poses
        # Render and save each

        while True:
            dset = random.choice(list(self.lvlsByName.keys()))
            lvl  = random.choice(list(self.lvlsByName[dset]))
            tile = random.choice(self.lvlsByName[dset][lvl])

            try:
                tileData = self.loadTile(tile)
                altTileData = self.loadTile(tile[:self.args.altLevel])
                # print(' - loaded', tile)
                break
            except FileNotFoundError:
                print(' - failed on', tile)
        altTileMeanAlt = altTileData.meanAlt()

        hfov = np.deg2rad(np.random.sample() * 30 + 30) # Between 30 and 60
        fx = fy = self.wh / np.tan(hfov * .5)
        cx, cy = self.wh / 2, self.wh / 2

        # The middle, in unit ecef
        anchor = tileData.middle()
        extent = tileData.extent()
        R = tileData.ltp()

        aglAlt = (np.random.sample() * .4 + .8) * extent / np.tan(hfov * .5)
        z = altTileMeanAlt + aglAlt
        xy = np.random.sample(2) * extent * .7
        local_p = np.array((*xy,z))

        print(' - anchor', anchor)
        print(' - n_anchor', normalize1(anchor))
        print(' - extent', extent, 'lvl', len(tile))
        print(' - R\n', R)

        R = R @ quat_to_matrix(quat_exp(generate_random_rvec(3.141, axisWeight=(1e-5,1e-5,1))))
        p = anchor + R.T @ (local_p)
        base_pose4 = add_row(np.hstack((R,p[:,np.newaxis])))

        res = PoseSet()
        res.base = np.hstack((R,p[:,np.newaxis]))


        for i in range(1):
            chaos = 1 + i
            rvec = generate_random_rvec(.05 * chaos)
            dp = (np.random.normal(size=3) * .2).clip(-1,1) * extent * chaos
            print(' - dp', dp)

            Rn = R @ quat_to_matrix(quat_exp(rvec))
            pn = anchor + Rn.T @ (local_p + dp)
            perturbed_pose = np.hstack((Rn,pn[:,np.newaxis]))
            res.perturbedPoses.append(perturbed_pose)
            # print(' - Relative Pose:\n', np.linalg.inv(add_row(perturbed_pose)) @ base_pose4)
            res.logDiffs.append(np.concatenate((dp,rvec)))
            res.cameraIntrin = [self.wh,self.wh, fx,fy, cx,cy]
        return res


    def run(self):
        entries = []
        for i in range(self.args.N):
            ent = self.generate_one()
            entries.append(ent)

        d = {'entries': [ent.pre_serialize() for ent in entries]}
        try: os.makedirs(self.args.outDir)
        except: pass
        with open(os.path.join(self.args.outDir, 'entries.json'), 'w') as fp:
            fp.write(json.dumps(d))
            fp.write('\n')



    def loadTile(self, tile):
        f = os.path.join(self.args.srcDir, 'node', tile)
        with open(f,'rb') as fp:
            nd = RT.NodeData.FromString(fp.read())
            return TileData(nd)

def main():
    parser = ArgumentParser()
    parser.add_argument('--srcDir', required=True)
    parser.add_argument('--outDir', required=True)
    parser.add_argument('--N', default=1000, type=int)
    parser.add_argument('--minAlt', default=40, type=float)
    parser.add_argument('--maxAlt', default=1500, type=float)
    parser.add_argument('--minLvl', default=14, type=int)
    parser.add_argument('--maxLvl', default=22, type=int)
    parser.add_argument('--altLevel', default=13, type=int)
    parser.add_argument('--wh', default=512, type=int)
    args = parser.parse_args()

    gen = Generator(args)
    gen.run()


if __name__ == '__main__':
    np.random.seed(0)
    random.seed(0)
    main()
