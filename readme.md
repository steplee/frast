# Frast
<b>F</b>ast <b>rast</b>er database.

GDAL is great and all but it is not good for storing hierarchical data aligned to a certain coordinate system in a single file.

On top of that, the `[multiple tiffs] -> VRT -> gdalwarp -> gdalcalc -> gdaladdo` pipeline I find myself using to get a single tiff can take days to run on a 100'000² pixel AOI.

So this suite of programs and libraries attempts to be better for the specific circumstance of storing many JPEG tiles into a single file at multiple levels under one projection.

It offers tools to convert from a GDAL supported format to an internal one (using gdal itself), and a library that amongst others, allows something equivalent to `RasterIO`.

The library stores JPEG tiles (for rgb/rgba/grayscale data) or `deflate`d tiles (for uint16 terrain data). The default and only supported tile size is 256². The projection is Web Mercator. Levels zero to 29 are allowed, but anything after 22 or so is unrealistic.

### Storage
The aim was to keep the file structure as simple as possible, and to make it easy to use with `mmap`.

The important part of the file format is a **completely flat* triplet of arrays. There are `keys`, `k2vs` and `values`. The `keys` are 64-bit integers encoding spatial location. The `k2vs` tell how far from a base pointer the data pertaining to each key lies. Finally, `vals` is just a binary blob.

The interesting part is how this file is created. We want something like `std::vector`, an array that can expand. We can guarantee that **all keys are added in order**.
To start with, I `mmap` a big range. Then keys and compressed images are added to it. To prevent copying like `std::vector`, which would be awfully slow and require atleast 2x free disk space, I use `fallocate` with the `FALLOC_FL_INSERT_RANGE` mode. This allows extending each of the three arrays individually. The only immediate caveat is the arrays need to be block aligned, which is no bid deal. A secondary caveat is that the resulting files will be fragmented. This is not a big deal either: just use `e4defrag` or copy the file to another partition/disk and back.

> TODO: Benchmark these three cases
> 1. Reading from random file locations using seek() and read()
> 2. Reading from random file locations using mmap accesses
> 3. Reading from random file locations using mmap accesses with madvise() calls to prefetch.

### `FlatReaderCached`
The main way to use an already populated dataset is through `FlatReaderCached`. This offers a `rasterIo` function similar to that of GDAL, only it takes an AABB in Web Mercator coords. Can also get tiles individually or in groups.

### frastpy2
There's python bindings that are largely self explanatory.


# FrastGL
Besides the frast data storage code, there is a code to render tiled datasets in OpenGL. There is a CRTP base class `GtRenderer` for this purpose, which takes a struct `GtTypes` with a bunch of implementation specific classes that must be provided. There's two implementations `rt` data from Google Earth, and `ftr` for the frast data.

> Note: The tile renderer was originally built around an in-repo Vulkan framework, which I then reverted to OpenGL without re-writing from scratch, leaving the `gt` code uncessarily complex.

Both `rt` and `ftr` support "casting", which uses a seperate shader that takes upto two more textures and projection matrices and renders those textures from the perspective of the projeciton matrices. This can be used to overlay a video from a camera that was on an aircraft for example, while also rendering the true data underneath. You could do this in multiple passes, but the way its implemented is with one pass that takes all the matrices and textures up front and blends the overlaid video directly in one shader.

My original code had two different implementations for `rt` and `ftr`, but this had a lot of near-duplicate code. So I tried to find what was common and what was unique and how to share as much code as possible. `GtRenderer` makes use of CRTP. This is my first time using it for anything non-trivial. You need to specialize a bunch of stuff, and I ended up having to add things as I went, but overall I'm happy with how it turned out (for the Vulkan version at least, now it is **way** to complex)

My original code also did not have access to bounding boxes to evaluate screen-space-error until the tiles were loaded from disk. This means each level had to be loaded sequentially! By precomputing the oriented bounding boxes, you can shortcut levels and directly load ancestors. This requires an extra prep step which is not ideal, but worth it (see `./frastgl/py/exportObbs.py`)

# Dockerization
### TODO: Update this



#### FrastGL Sample Images
![fvk rt example](/docs/fvk_rt.jpg)

#### Exporting Bounding Boxes
As mentioned above, the `GtRenderer` requires a precomputed bounding box for every tile. This is accomplished by the python script `exportObbs.py`. The initial bounding box implementation is just a tile id followed by a tuple of `<ctr, extent, quaternion>`.
For frast datasets, the command would be something like:
```
python3 -m pysrc.exportObbs --frastColor /data/naip/vegas2020/vegas2020.ft --frastElev /data/elevation/srtm/usa.11.ft
```
For rt, something like:
```
python3 -m pysrc.exportObbs --gearthDir /data/gearth/naipAoisWgs/ --gearthBulkDirOverride /data/gearth/naipAoisWgs/bulk
```
Now, there will be an `index.v1.bin` alongside the input file
