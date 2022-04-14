import numpy as np, os, sys, cv2, random
from argparse import ArgumentParser
from multiprocessing import Pool

import proto.rocktree_pb2 as RT
from utils import *
from decode import *


class TileData:
    def __init__(self, nd):
        self.nd = nd

        mesh = nd.meshes[0]
        verts = decode_verts(mesh.vertices)
        M = np.array((nd.matrix_globe_from_mesh)).reshape(4,4).T
        verts = verts.astype(np.float64) @ M[:3,:3].T + M[:3,3]

        self.middle_ = verts.mean(0)

        self.LTP_ = lookAtR(self.middle, np.zeros(3), np.array((0,0,1.)))

        verts_ltp = verts @ self.LTP_
        verts_ltp = verts_ltp - verts.mean(0,keepdims=True)
        self.extent_ = (verts_ltp[:2].max(0) - verts_ltp[:2].min(0)).max()

        some_geodetics = np.stack((geodetic_to_ecef(*c) for c in verts[::4]))
        self.meanAlt_ = some_geodetics[:,2].mean()

    def middle(self): return self.middle_
    def extent(self): return self.extent_
    def meanAlt(self): return self.meanAlt_
    def ltp(self): return self.LTP_

class Generator():
    def __init__(self, args):

        nodeFiles = []
        for f in os.listdir(os.path.join(args.srcDir, 'node')):
            nodeFiles.append(os.path.join(args.srcDir,'node',f))

        lvlsByName = {}
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
            for lvl,arr in lvls.items(): print('\t\t lvl {}: {} nodes'.format(lvl,len(arr)))

        self.nodeFiles, self.lvlsByName = nodeFiles, lvlsByName
        self.args = args

    def generate_one(self):
        # Pick a dset, level, and tile
        # Get the corresponding ancestor tile in the altitudeLevel
        # Sample reasonable intrin, AGL altitude, and orientation (check bounds)
        # Compute initial pose
        # Generate M perturbed poses
        # Render and save each

        dset = random.choice(list(self.lvlsByName.keys()))
        lvl  = random.choice(list(self.lvlsByName[dset]))
        tile = random.choice(self.lvlsByName[dset][lvl])

        tileData = self.loadTile(tile)
        altTileData = self.loadTile(tile[:self.args.altLevel])
        altTileMeanAlt = altTileData.meanAlt()

        hfov = np.deg2rad(np.random.sample() * 30 + 30) # Between 30 and 60
        fx = fy = self.wh / np.tan(hfov * .5)

        # The middle, in unit ecef
        anchor = tileData.middle()
        extent = tileData.extent()

        # LTP matrix
        R = lookAt(anchor, np.zeros(3), np.array((0,0,1.)))

        aglAlt = (np.random.sample() * .4 + .8) * extent / np.tan(hfov * .5)
        z = altTileMeanAlt + aglAlt
        xy = np.random.sample(2) * extent * .7
        local_p = np.array((*xy,z))

        p = anchor + R.T @ local_p


    def run(self):
        for i in range(self.args.N):
            self.generate_one()

    def loadTile(self, tile):
        f = os.path.join(self.args.srcDir, 'node', tile)
        with open(f,'rb') as fp:
            nd = RT.NodeData.FromString(fp.read())
            return TileData(nd)

def main():
    parser = ArgumentParser()
    parser.add_argument('--srcDir', required=True)
    parser.add_argument('--outDir', required=True)
    parser.add_argument('--N', default=1000)
    parser.add_argument('--minAlt', default=40)
    parser.add_argument('--maxAlt', default=1500)
    parser.add_argument('--minLvl', default=14)
    parser.add_argument('--maxLvl', default=22)
    parser.add_argument('--altLevel', default=13)
    parser.add_argument('--wh', default=13)
    args = parser.parse_args()

    gen = Generator(args)
    gen.run()

if __name__ == '__main__':
    main()
