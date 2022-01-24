import numpy as np
import frastpy
import cv2

o = frastpy.DatasetReaderOptions()
d = frastpy.DatasetReader('out', o)

#tlbr = [-8599290.943935, 4750316.582839, -8598122.604486, 4751209.204594 ]
tlbr = [-8599290.943935, 4750316.582839]
sz = 6000
sz = 1000
tlbr.append(tlbr[0] + sz)
tlbr.append(tlbr[1] + sz)

quad = np.array([
        *tlbr[:2],
        tlbr[2], tlbr[1],
        *tlbr[2:],
        tlbr[0], tlbr[3]
        ])

img1 = np.zeros((256*2,2*256,3), dtype=np.uint8)
#cv2.imshow(' - Fetched 1 -', img1)

ctr = np.array(tlbr).reshape(2,2).mean(0)[np.newaxis]

img1 = cv2.cvtColor(img1,cv2.COLOR_RGB2BGR)

for t in np.linspace(0, 1, 1500):
    off = 8000 * np.array((np.cos(t), np.sin(t)))[np.newaxis]
    R = np.array((np.cos(t), -np.sin(t), np.sin(t), np.cos(t))).reshape(2,2)
    #R = np.eye(2)
    quad1 = ((quad.reshape(4,2) - ctr) @ R.T + ctr + off).reshape(-1)
    img2 = (d.rasterIoQuad(img1, quad1))

    img2 = cv2.cvtColor(img2,cv2.COLOR_RGB2BGR)
    cv2.imshow(' - Fetched 2 -', img2)
    cv2.waitKey(1)
