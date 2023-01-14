import sys, os
sys.path.append(os.path.join(os.getcwd(), 'build'))

import frastpy2
import cv2

fname = '/data/naip/mocoNaip/moco.fft'

opts = frastpy2.EnvOptions()
dset =  frastpy2.FlatReaderCached(fname, opts)

for k,v in dset.iterTiles(12, 3):
    print(' - iter k', k)
    cv2.imshow('img',v)
    cv2.waitKey(11)

print(' - showing result of getTile() on ', 3458765874251433104)
v = dset.getTile(3458765874251433104, 1)
cv2.imshow('img',v)
cv2.waitKey(0)
