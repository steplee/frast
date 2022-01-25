import numpy as np
import frastpy
import cv2

o = frastpy.DatasetReaderOptions()
d = frastpy.DatasetReader('out.ft', o)

tlbr = [37400, 81080, 37400+2, 81080+2]
tlbr = np.array(([tlbr,tlbr]))
tlbr = tlbr[0:1] # This fails
tlbr = tlbr[0]   # this works
img1 = np.zeros((256*4,4*256,3), dtype=np.uint8)
# Copy required, img3 overwrites the img1 buffer below.
img2 = np.copy(d.fetchBlocks(img1, 17, tlbr, False), 'C')
tlbr = [37400, 81080, 37400+3, 81080+3]
img3 = d.fetchBlocks(img1, 17, tlbr, False)

# Acessing invalid pixels, this will silently fail,
# returning a black image.
badImg = d.fetchBlocks(img1, 17, [1,1,2,2], False)
#print(badImg)

print(img1.shape)
print(img2.shape)
img1,img2,img3 = (cv2.cvtColor(i,cv2.COLOR_RGB2BGR) for i in (img1,img2,img3))
cv2.imshow(' - Fetched 1 -', img1)
cv2.imshow(' - Fetched 2 -', img2)
cv2.imshow(' - Fetched 3 -', img3)
cv2.imshow(' - Fetched 4 (bad) -', badImg)
cv2.waitKey(0)
