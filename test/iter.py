
import numpy as np
import frastpy
import cv2

o = frastpy.DatasetReaderOptions()
#d = frastpy.DatasetReader('/data/naip/mocoNaip/out.ft', o)
d = frastpy.DatasetReader('out2.ft', o)

codes = []
strs = []
for it in d.iterTiles(13):
    bc = it[0]
    str1 = 'z{} y{} x{}'.format(bc.z(),bc.y(),bc.x())
    str2 = 'c' + bin(bc.c())[2:][:6]
    str3 = ' ' + bin(bc.c())[2:][-29*2:-29]
    str4 = ' ' + bin(bc.c())[2:][-29:]
    codes.append(bc.c())
    strs.append(bin(bc.c())[2:])
    img = cv2.cvtColor(it[1], cv2.COLOR_RGB2BGR)
    cv2.putText(img, str1, (20,20), 0, .5, (255,255,255))
    cv2.putText(img, str1, (21,21), 0, .5, (0,0,0))
    cv2.putText(img, str2, (4,40), 0, .4, (255,255,255))
    cv2.putText(img, str3, (4,50), 0, .4, (255,255,255))
    cv2.putText(img, str4, (4,60), 0, .4, (255,255,255))
    cv2.imshow('tile',img)
    cv2.waitKey(0)

print('codes:', np.argsort(codes))
print('strs :', np.argsort(strs))
strs2 = []
for s in strs: strs2.append([len(strs2),s])
print('strs :', [k[0] for k in sorted(strs2, key=lambda k:k[1])])
print(sorted(strs) == strs)
