
import numpy as np
import frastpy
import torch

o = frastpy.DatasetReaderOptions()
d = frastpy.DatasetReader('out.ft', o)

x = torch.randn(5).cuda()
y = d.doTensor(x)
print(x)
print(y)
