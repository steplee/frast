#!/bin/bash


DIR=/data/dawn
mkdir $DIR || echo "ok";
pushd "$DIR"

git clone https://dawn.googlesource.com/dawn/ --single-branch --branch=main --depth=1 --shallow-submodules
cd dawn
git submodule update --init
pushd third_party/vulkan-deps
git submodule update --init --recursive
popd

sed -i "s/add_subdirectory(third_party)/set\(BUILD_SHARED_LIBS\ OFF)\\nadd_subdirectory\(third_party\)\\nset\(BUILD_SHARED_LIBS\ ON)/" ./CMakeLists.txt
mkdir build

cd build
cmake .. -DTINT_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=RELEASE -DCMAKE_INSTALL_PREFIX=/data/dawn/ -DDAWN_ENABLE_INSTALL=ON -DTINT_ENABLE_INSTALL=ON
make -j6
sed -i '/libtint_lang_msl_writer_raise/d' src/tint/cmake_install.cmake
sed -i '/libtint_utils_file/d' src/tint/cmake_install.cmake
make install --ignore-errors
cp ./third_party/glfw/src/libglfw3.a /data/dawn/lib
cp ./src/dawn/glfw/libdawn_glfw.a /data/dawn/lib
popd
