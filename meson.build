project('frast', ['cpp'],
    default_options : [
      'cpp_std=c++17',
      'buildtype=debugoptimized',
      # 'optimization=2',
      # 'python.platlibdir=@0@'.format(get_option('prefix')) / 'lib/python3/dist-packages/',
      # 'python.purelibdir=@0@'.format(get_option('prefix')) / 'lib/python3/dist-packages/',
      # Mark invalid, require user to set them manually, because `get_option('prefix')` *DOES NOT* get the user configured prefix path :(
      'python.platlibdir=invalid',
      'python.purelibdir=invalid',
      ]
    )

assert(get_option('python.platlibdir') != 'invalid', 'must set python.platlibdir')
assert(get_option('python.purelibdir') != 'invalid', 'must set python.purelibdir')

fs = import('fs')
cmake = import('cmake')

set_warnings = [
  # Turn on all warnings
  '-Wall',
  # Make critical warnings errors
  '-Werror=return-type',
  # Turn off annoying warnings that are not indicative of bugs
  '-Wno-reorder',
  '-Wno-unused_function',
  '-Wno-unused-function',
  '-Wno-unused-variable',
  '-Wno-unused-but-set-variable',
  '-Wno-deprecated-declarations',
]
add_project_arguments(set_warnings, language : 'cpp')

frast_flags = ['-DFRAST_WRITER_THREADS=@0@'.format(get_option('writer_threads'))]

eigen_dep = dependency('eigen3')
# eigen_dep = declare_dependency(include_directories: ['/usr/include/eigen3'])
threads_dep = dependency('threads')
dl_dep = dependency('dl')


python3 = import('python').find_installation('python3')
python3_path = python3.path()
pybind11_config_ret = run_command(python3_path, ['-m', 'pybind11', '--includes'])
pybind11_includes = pybind11_config_ret.stdout().split('-I')[-1].strip()
pybind11_dep = declare_dependency(
    include_directories: [pybind11_includes],
)


fmt_dep = dependency('fmt', required: true)



# https://github.com/mesonbuild/meson/blob/master/test%20cases/frameworks/5%20protocol%20buffers/meson.build
if get_option('gl').enabled()
  protoc = find_program('protoc', required : true)
  gen = generator(protoc, \
    output    : ['@BASENAME@.pb.cc', '@BASENAME@.pb.h'],
    # arguments : ['--proto_path=@CURRENT_SOURCE_DIR@', '--cpp_out=@BUILD_DIR@', '@INPUT@'])
    arguments : ['--proto_path=@CURRENT_SOURCE_DIR@/frast2/frastgl/gt/rt/protos/', '--cpp_out=@BUILD_DIR@', '@INPUT@'])
  rocktree_pb_cc = gen.process('frast2/frastgl/gt/rt/protos/rocktree.proto')
endif

# If we appear to have a custom opencv4 build specifically for terrapixel stuff (located
# in '/usr/local/terrapixel/') then use it, otherwise use meson to find the system's
# installation (pkgconfig or cmake)
if fs.is_dir('/usr/local/terrapixel/include/opencv4')
  opencv_root = '/usr/local/terrapixel/'
  opencv_inc_dir = opencv_root / 'include' / 'opencv4'
  opencv_lib_dir = opencv_root / 'lib'
  opencv_dep = declare_dependency(
    include_directories: include_directories(opencv_inc_dir),
    dependencies: [
      meson.get_compiler('cpp').find_library('opencv_core', dirs: opencv_lib_dir),
      meson.get_compiler('cpp').find_library('opencv_highgui', dirs: opencv_lib_dir),
      meson.get_compiler('cpp').find_library('opencv_imgproc', dirs: opencv_lib_dir),
      meson.get_compiler('cpp').find_library('opencv_imgcodecs', dirs: opencv_lib_dir),
    ])
else
  # NOTE: This might pick the debug version, which I do not want.
  opencv_dep = dependency('opencv4')
endif

# gdal_dep = declare_dependency(dependencies: [
      # meson.get_compiler('cpp').find_library('gdal')
      # ])
gdal_dep = dependency('gdal', required: true)
z_dep = declare_dependency(dependencies: [
      meson.get_compiler('cpp').find_library('z')
      ])

if get_option('gl').enabled()
  protobuf_dep = dependency('protobuf')

  glfw_opts = cmake.subproject_options()
  glfw_opts.add_cmake_defines({'GLFW_BUILD_EXAMPLES': false})
  glfw_opts.add_cmake_defines({'GLFW_BUILD_TESTS': false})
  glfw_opts.add_cmake_defines({'GLFW_BUILD_WAYLAND': false})
  glfw_opts.add_cmake_defines({'GLFW_INSTALL': false})
  glfw = cmake.subproject('glfw', options: glfw_opts)
  glfw_dep = glfw.dependency('glfw')

  gl_dep = declare_dependency(
    dependencies: [
        # meson.get_compiler('cpp').find_library('glfw3', dirs: '/usr/local/terrapixel/lib'),
        glfw_dep,
        meson.get_compiler('cpp').find_library('GL'),
        meson.get_compiler('cpp').find_library('GLEW'), ])
endif

# ----------------------------------------------
#               Libraries
# ----------------------------------------------

frast = shared_library('frast',
  files(
    'frast2/tpool/tpool.cc',
    'frast2/flat/codec.cc',
    'frast2/flat/flat_env.cc',
    'frast2/flat/reader.cc',
    'frast2/flat/writer.cc',
    'frast2/flat/writer_addo.cc',
    'frast2/flat/writer_gdal.cc',
    'frast2/flat/writer_gdal_many.cc',
    ),
  include_directories: include_directories('frast2'),
  dependencies: [eigen_dep, fmt_dep, threads_dep, opencv_dep, gdal_dep, z_dep],
  cpp_args: frast_flags,
  install: true
  )

frast_incs = include_directories(
  '.',
  'frast2/frastgl/core/imgui/generated',
  'frast2/frastgl/core/implot/generated')

frast_dep = declare_dependency(
  link_with: [frast],
  include_directories: frast_incs,
  dependencies: [eigen_dep, fmt_dep, threads_dep, opencv_dep])


if get_option('gl').enabled()
  frastgl = shared_library('frastgl',
    files(
      'frast2/frastgl/core/app.cc',
      'frast2/frastgl/core/cameras.cc',
      'frast2/frastgl/core/render_state.cc',
      'frast2/frastgl/core/shader.cc',
      'frast2/frastgl/core/shaders.cc',
      'frast2/frastgl/core/window.cc',
      'frast2/frastgl/extra/caster/castable.cc',
      'frast2/frastgl/extra/earth/earth.cc',
      'frast2/frastgl/extra/ellipsoid/ellipsoid.cc',
      'frast2/frastgl/extra/frustum/frustum.cc',
      'frast2/frastgl/extra/loaders/mesh.cc',
      'frast2/frastgl/extra/loaders/obj.cc',
      'frast2/frastgl/extra/textSet/textSet.cc',
      'frast2/frastgl/gt/ftr/ftr.cc',
      'frast2/frastgl/gt/rt/rt.cc',
      # 'frast2/frastgl/gt/rt/protos/rocktree.pb.cc',
      'frast2/frastgl/gt/gt.cc',

      'frast2/frastgl/gt/gdal/gdal.cc',
      'frast2/frastgl/gt/gdal/dataset.cc',
      'frast2/frastgl/gt/gdal/create_obb.cc',

      'frast2/frastgl/core/imgui/imgui_app.cc',

      'frast2/frastgl/core/imgui/generated/imgui.cpp',
      # 'frast2/frastgl/core/imgui/generated/imgui_demo.cpp',
      'frast2/frastgl/core/imgui/generated/imgui_draw.cpp',
      'frast2/frastgl/core/imgui/generated/imgui_tables.cpp',
      'frast2/frastgl/core/imgui/generated/imgui_widgets.cpp',
      'frast2/frastgl/core/imgui/generated/imgui_impl_glfw.cpp',
      'frast2/frastgl/core/imgui/generated/imgui_impl_opengl3.cpp',
      'frast2/frastgl/core/implot/generated/implot.cpp',
      'frast2/frastgl/core/implot/generated/implot_items.cpp',
      # 'frast2/frastgl/core/implot/generated/implot_demo.cpp',
      ),
      rocktree_pb_cc,
    include_directories: include_directories('frast2'),
    # link_with: [copy_imgui_files],
    dependencies: [frast_dep, gl_dep, protobuf_dep, dl_dep, gdal_dep],
    cpp_args: frast_flags,
    install: true)

  frastgl_dep = declare_dependency(
    # include_directories: include_directories('frast2'),
    link_with: [frastgl],
    dependencies: [frast_dep, gl_dep, protobuf_dep, gdal_dep],
    compile_args: ['-DFRASTGL=1'],
    )
else
  frastgl_dep = declare_dependency()
endif

install_subdir('frast2', install_dir: 'include', exclude_files: ['*.cc', '*.cpp', '*.py', '*.pyc'])

py_files = files('frast2/frastpy2/frastpy2_c.cc')
if get_option('gl').enabled()
  py_files += files('frast2/frastpy2/gt_app_wrapper.cc')
endif

python3.extension_module('frastpy2_c',
  py_files,
  subdir: 'frastpy2',
  install: true,
  dependencies: [frast_dep, frastgl_dep],
  link_with: [])
install_subdir('frast2/frastpy2', install_dir: 'lib/python3/dist-packages/', exclude_files: ['*.cc', '*.cpp'])

# ----------------------------------------------
#               Applications
# ----------------------------------------------

frastTool = executable('frastTool',
  files('frast2/tool/main.cc'),
  dependencies: [frast_dep, gdal_dep],
  cpp_args: frast_flags,
  install: true
  )

frastFlatWriter = executable('frastFlatWriter',
  files('frast2/flat/convertMain.cc'),
  dependencies: [frast_dep],
  cpp_args: frast_flags,
  install: true)


if get_option('gl').enabled()
  runFtr = executable('runFtr',
    files('frast2/frastgl/gt/ftr/runFtr.cc'),
    dependencies: [frastgl_dep],
    install: false)
  runRtr = executable('runRt',
    files('frast2/frastgl/gt/rt/runRt.cc'),
    dependencies: [frastgl_dep],
    install: false)
  runGdal = executable('runGdal',
    files('frast2/frastgl/gt/gdal/runGdal.cc'),
    dependencies: [frastgl_dep],
    install: false)


  if get_option('imgui_demo').enabled()
  runRtr = executable('runImguiDemo',
    files('frast2/frastgl/core/imgui/imgui_demo_app.cc',
      'frast2/frastgl/core/imgui/generated/imgui_demo.cpp',
      'frast2/frastgl/core/implot/generated/implot_demo.cpp',
      ),
    dependencies: [frastgl_dep],
    install: false)
  endif
endif

if true
  executable('testDataset', files('frast2/frastgl/gt/gdal/testDataset.cc'), dependencies: [frastgl_dep])
endif
