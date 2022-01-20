import numpy as np
import frastpy
import cv2

o = frastpy.DatasetReaderOptions()
d = frastpy.DatasetReader('out', o)

#tlbr = [-8599290.943935, 4750316.582839, -8598122.604486, 4751209.204594 ]
tlbr = [-8599290.943935, 4750316.582839]
tlbr.append(tlbr[0] + 4000)
tlbr.append(tlbr[1] + 4000)
tlbr = np.array(tlbr)

img1 = np.zeros((256*2,2*256,3), dtype=np.uint8)
#cv2.imshow(' - Fetched 1 -', img1)

img1 = cv2.cvtColor(img1,cv2.COLOR_RGB2BGR)

for t in np.linspace(0, 50, 1000):
    off = 2000 * np.array((np.cos(t), np.sin(t)))
    tlbr_ = tlbr + np.array((*off, *(off*2.)))
    img2 = (d.rasterIo(img1, tlbr_))

    img2 = cv2.cvtColor(img2,cv2.COLOR_RGB2BGR)
    cv2.imshow(' - Fetched 2 -', img2)
    #cv2.imshow(' - Fetched 4 (bad) -', badImg)
    cv2.waitKey(33)

