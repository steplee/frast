# Frast
<b>F</b>ast <b>rast</b>er database.

GDAL is great and all but it is not good for storing hierarchical data aligned to a certain coordinate system in a single file.
There is `gdal_grid.py`, but it requires a file per tile.

On top of that, the `[multiple tiffs] -> VRT -> gdalwarp -> gdalcalc -> gdaladdo` pipeline I find myself using to get a single tiff can take days to run on a 100'000² pixel AOI.

So this suite of several programs and one static library attempts to be better for the specific circumstance of storing many JPEG tiles into a single file at multiple levels under one projection.

It offers tools to convert from a GDAL supported format to an internal one (using gdal itself), and a library that amongst others, allows something equivalent to `RasterIO`.

# FrastVk
Besides the frast data storage code, there is a completely seperate Vulkan framework to help build applications, called `fvk`. This should really be its own repo. There are basic Vulkan wrapper types that take care of handling tedious Vulkan structs and object management. There are also some classes to help render various things. The most substantive is the `GtRenderer` class, which is a generic tree-based level-of-detailing renderer. There are two specializations: `rt` and `ftr`. `ftr` stands for "frast tiled renderer", it renders 2.5 WebMercator color and DTED tiles. `rt` renders google-earth data. The downloader is elsewhere...

Both `rt` and `ftr` support "casting", which uses a seperate shader that takes upto two more textures and projection matrices and renders those textures from the perspective of the projeciton matrices. This can be used to overlay a video from a camera that was on an aircraft for example, while also rendering the true data underneath. You could do this in multiple passes, but the way its implemented is with one pass that takes all the matrices and textures up front and blends the overlaid video directly in one shader.

My original code had two different implementations for `rt` and `ftr`, but this had a lot of near-duplicate code. So I tried to find what was common and what was unique and how to share as much code as possible. `GtRenderer` makes use of CRTP. This is my first time using it for anything non-trivial. You need to specialize a bunch of stuff, and I ended up having to add things as I went, but overall I'm happy with how it turned out.

My original code also did not have access to bounding boxes to evaluate screen-space-error until the tiles were loaded from disk. This means each level had to be loaded sequentially! By precomputing the oriented bounding boxes, you can shortcut levels and directly load ancestors. This requires an extra prep step which is not ideal, but worth it (see `./frastVk/pysrc/exportObbs.py`)

All shaders must follow the `frastVk/shaders/<group>/<name>.<type>.glsl`. They can be compiled (assuming zsh) with:
```
python3 -m pysrc.compile_shaders --srcFiles frastVk/shaders/**/*.glsl --dstName frastVk/shaders/compiled/all.cc --targetEnv='--target-env vulkan1.2'
```

### No Frame Overlap
FrastVk does not support frame overlap. That is, even though the swapchain has three or more entries, only one frame can be rendering at once (of course one is also being presented, so it is double buffered). To get as many frames as possible, Vulkan applications can actually render more than one frame ahead of time. But this requires N-way buffering *all* UBOs, textures, etc. which is outside the scope of frastVk. Doing all that extra buffering is not worth the trouble, considering how smooth the results already are. Besides that, it is usually impossible to get future data to even fill the buffers with! I guess the idea of a framework with frame-overlap is to render as many frames with inter/extrapolated data not just to get more frames, but so that the data is always most recent -- even if it is not as valid.

# Dockerization
Building with docker is currently support on x864_64. I need to remove  OpenCV because it is a massive depedency. (*update: done. Unfortunately libgdal is still requires a bunch of shared objects present...) You must download the Vulkan SDK version 1.3.211 and put `./docker/dist/`, then use
```
sudo docker build -t frast:0.9 -f docker/Dockerfile.dev .
```


### Dependencies
  - Eigen
  - LMDB
  - Clang


# FrastVk Notes:
  - There is a slight issue with my tree implementation for both `tiled_renderer2` and `rt`. It requires all children to close before closing the parent. This means that while rendering a deep level, a bunch out-of-frame but still high-res tiles must be resident.
  - The rocktree data includes an octant mask to help with, but I currently don't use it. Basically the vertices are organized in a way that allows rendering e.g. 3/8 tiles and not requiring having the sibiling still loaded. The parent can be partially rendered with the other 5/8 parts. This is a better approach.

#### FVK RT Sample Images
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


### TODO
  - ~~Fix the bilinear interpolation in convertGdal.cc. It works when the number of sample points is large (128), but it should work for a very small grid too (like 4x4).~~
     - This was fixed by writing my own interpolation, see `my_remapRemap`
  - Allow resizing db when it grows too large, which also would not require inputting large enough size at creation time.
  - Support output to a cuda buffer.
  - ~~Support sampling an arbitrary quad (rather than just a bbox in rasterIo)~~, done, see `rasterIoQuad`
  - Explore if there is a benefit to exporting the lmdb to a completely flat read-only format, with a hashmap in the header that maps to file offsets of tiles.
  - Make convertGdal faster if input is already WebMercator (no need to warp)
