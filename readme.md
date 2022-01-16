# Frast
<b>F</b>ast <b>rast</b>er database.

GDAL is great and all but it is not good for storing hierarchical data aligned to a certain coordinate system in a single file.
There is `gdal_grid.py`, but it requires a file per tile.

On top of that, the `[multiple tiffs] -> VRT -> gdalwarp -> gdalcalc -> gdaladdo` pipeline I find myself using to get a single tiff can take days to run on a 100'000² pixel AOI.

So this suite of several programs and one static library attempts to be better for the specific circumstance of storing many JPEG tiles into a single file at multiple levels under one projection.

It offers tools to convert from a GDAL supported format to an internal one (using gdal itself), and a library that amongst others, allows something equivalent to `RasterIO`.


### Dependencies
  - Eigen
  - LMDB
  - OpenCV 4+
  - Clang


### TODO
  - Python bindings, optimized for PyTorch DataLoaders
  - `gdaladdo` equivalent
  - `RasterIO` equivalent
  - Explore if there is a benefit to exporting the lmdb to a completely flat read-only format, with a hashmap in the header that maps to file offsets of tiles.
