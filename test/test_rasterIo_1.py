import numpy as np
import frastpy
import cv2

o = frastpy.DatasetReaderOptions()

d = frastpy.DatasetReader('out', o)

#tlbr = [-8599290.943935, 4750316.582839, -8598122.604486, 4751209.204594 ]
tlbr = [-8599290.943935, 4750316.582839]
tlbr.append(tlbr[0] + 4000)
tlbr.append(tlbr[1] + 4000)
tlbr = np.array(([tlbr,tlbr]))
tlbr = tlbr[0:1] # This fails
tlbr = tlbr[0]   # this works
img1 = np.zeros((256*2,2*256,3), dtype=np.uint8)
# Copy required, img3 overwrites the img1 buffer below.
#img2 = np.copy(d.rasterIo(img1, tlbr), 'C')
img2 = (d.rasterIo(img1, tlbr))

# Acessing invalid pixels, this will silently fail,
# returning a black image.
#badImg = d.rasterIo(img1,  [1,1,2,2])
#print(badImg)

print(img1.shape)
print(img2.shape)
img1,img2 = (cv2.cvtColor(i,cv2.COLOR_RGB2BGR) for i in (img1,img2))
cv2.imshow(' - Fetched 1 -', img1)
cv2.imshow(' - Fetched 2 -', img2)
#cv2.imshow(' - Fetched 4 (bad) -', badImg)
cv2.waitKey(0)
