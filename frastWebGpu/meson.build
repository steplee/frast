project('frastWebGpu', ['cpp'],
    default_options : [
      # 'cpp_std=c++17',
      'buildtype=debugoptimized',
      # 'optimization=2',
      # 'python.platlibdir=@0@'.format(get_option('prefix')) / 'lib/python3/dist-packages/',
      # 'python.purelibdir=@0@'.format(get_option('prefix')) / 'lib/python3/dist-packages/',
      # Mark invalid, require user to set them manually, because `get_option('prefix')` *DOES NOT* get the user configured prefix path :(
      # 'python.platlibdir=invalid',
      # 'python.purelibdir=invalid',
      ]
    )

if get_option('webgpu').enabled()

  if get_option('emscripten').enabled()
    # emcc = meson.get_compiler('emcc')
    assert (target_machine.cpu() == 'wasm32')
  endif

  dawn_dir = get_option('dawn_dir')
  dawn_include = dawn_dir / 'include'
  dawn_libdir = dawn_dir / 'lib'

  if get_option('dawn_dir') != ''
    if host_machine.system() != 'emscripten'

      # This is too complicated (uses git submodules).
      # I'll have to use a regular script out of the build system instead.
      '''
      dawn_opts = cmake.subproject_options()
      # dawn_opts.add_cmake_defines({'GLFW_BUILD_EXAMPLES': false})
      # dawn_opts.add_cmake_defines({'GLFW_BUILD_TESTS': false})
      dawn_opts.add_cmake_defines({'DAWN_BUILD_SAMPLES': false})
      dawn_opts.add_cmake_defines({'TINT_BUILD_TESTS': false})
      dawn = cmake.subproject('dawn', options: dawn_opts)
      dawn_dep = dawn.dependency('glfw')
      '''

      dawn_libs = [
          # meson.get_compiler('cpp').find_library('dawn_native', dirs: dawn_libdir),
          # meson.get_compiler('cpp').find_library('dawncpp', dirs: dawn_libdir),
          # meson.get_compiler('cpp').find_library('webgpu_dawn', dirs: dawn_libdir),
          # meson.get_compiler('cpp').find_library('dawn_proc', dirs: dawn_libdir),
          # meson.get_compiler('cpp').find_library('dawn_platform', dirs: dawn_libdir),
          # meson.get_compiler('cpp').find_library('dawn_headers', dirs: dawn_libdir),
          # meson.get_compiler('cpp').find_library('dawncpp_headers', dirs: dawn_libdir),
          # meson.get_compiler('cpp').find_library('dawn_wire', dirs: dawn_libdir),
          meson.get_compiler('cpp').find_library('dawn_glfw', dirs: dawn_libdir),

          meson.get_compiler('cpp').find_library('glfw', dirs: dawn_libdir),
          meson.get_compiler('cpp').find_library('dawn_proc', dirs: dawn_libdir),
          meson.get_compiler('cpp').find_library('dawn_platform', dirs: dawn_libdir),
          meson.get_compiler('cpp').find_library('webgpu_dawn', dirs: dawn_libdir),
          meson.get_compiler('cpp').find_library('dawn_native', dirs: dawn_libdir),
          meson.get_compiler('cpp').find_library('dawn_wire', dirs: dawn_libdir),
          meson.get_compiler('cpp').find_library('dawncpp', dirs: dawn_libdir),

        ]
      # dawn_glfw_lib = meson.get_compiler('cpp').find_library('dawn_glfw', dirs: dawn_libdir)
      # glfw_lib = meson.get_compiler('cpp').find_library('glfw', dirs: dawn_libdir)
      glfw_lib = meson.get_compiler('cpp').find_library('glfw3', dirs: dawn_libdir)

      dawn_main_dep = declare_dependency(
        dependencies: [
          dawn_libs
        ],
        include_directories: include_directories(
          dawn_include,
          dawn_dir / 'dawn/include',
          dawn_dir / 'dawn/src',
          dawn_dir / 'dawn/build/gen/include',
        )
      )

      dawn_wasm_dep = declare_dependency(
        dependencies: [
          # dawn_libs
        ],
        include_directories: include_directories(
          dawn_include,
          dawn_dir / 'dawn/include',
          dawn_dir / 'dawn/src',
          dawn_dir / 'dawn/build/gen/include',
        )
      )

      wget = find_program('wget')

      # webgpu_cpp_dawn_hpp = custom_target('webgpu_cpp_dawn_hpp',
        # output : 'webgpu_cpp_dawn.hpp',
        # command : [wget, 'https://raw.githubusercontent.com/eliemichel/WebGPU-Cpp/main/dawn/webgpu.hpp', '-O', '@OUTPUT@'],
        # )
      # webgpu_cpp_emscripten_hpp = custom_target('webgpu_cpp_emscripten_hpp',
        # output : 'webgpu_cpp_dawn.hpp',
        # command : [wget, 'https://raw.githubusercontent.com/eliemichel/WebGPU-Cpp/main/emscripten/webgpu.hpp', '-O', '@OUTPUT@'],
        # )
      # webgpu_cpp_emscripten_webgpu_h = custom_target('webgpu_cpp_emscripten_webgpu_h',
        # output : 'webgpu_cpp_dawn.hpp',
        # command : [wget, 'https://raw.githubusercontent.com/eliemichel/WebGPU-Cpp/main/emscripten/webgpu.h', '-O', '@OUTPUT@'],
        # )

      frastWebGpu = library('frastWebGpu',
        files(
          './webgpu_helpers.cc',
          )
            # + webgpu_cpp_dawn_hpp
            ,

        dependencies: [dawn_main_dep],
        )

      frastWebGpu_dep = declare_dependency(
        link_with: [frastWebGpu],
        dependencies: [dawn_main_dep])

      frastWebGpuNativeTest = executable(
        'frastWebGpuNativeTest',
        # files('./apps/webgpu_helpers.cc'),
        files('./apps/test_native.cc'),
        dependencies: [frastWebGpu_dep, glfw_lib])


      frastDawnTriangleGlfw = executable(
        'frastDawnTriangleGlfw',
        files('./apps/dawn_triangle.cc',
              dawn_dir / 'dawn/src/dawn/utils/ComboRenderPipelineDescriptor.cpp',
              dawn_dir / 'dawn/src/dawn/utils/ComboRenderBundleEncoderDescriptor.cpp',
              dawn_dir / 'dawn/src/dawn/utils/BinarySemaphore.cpp',
              dawn_dir / 'dawn/src/dawn/utils/SystemUtils.cpp',
              dawn_dir / 'dawn/src/dawn/utils/TextureUtils.cpp',
              dawn_dir / 'dawn/src/dawn/utils/WireHelper.cpp',
              dawn_dir / 'dawn/src/dawn/utils/TerribleCommandBuffer.cpp',
              dawn_dir / 'dawn/src/dawn/utils/WGPUHelpers.cpp',
              dawn_dir / 'dawn/src/dawn/samples/SampleUtils.cpp',
          ),
        # dependencies: [frastWebGpu_dep, glfw_lib])
        dependencies: [dawn_main_dep, glfw_lib],
        cpp_args: ['-DDAWN_ENABLE_BACKEND_VULKAN=1', '-DNDEBUG', '-fno-exceptions', '-fno-rtti']
        )

    endif

    if host_machine.system() == 'emscripten'

      dawn_main_dep = declare_dependency(
        dependencies: [
          # dawn_libs
        ],
        include_directories: include_directories(
          dawn_include,
          dawn_dir / 'dawn/include',
          dawn_dir / 'dawn/src',
          dawn_dir / 'dawn/build/gen/include',
        )
      )

      '''
      frastDawnTriangleWasm = executable(
        'frastDawnTriangleWasm',
        files('./apps/dawn_triangle.cc',
              dawn_dir / 'dawn/src/dawn/utils/ComboRenderPipelineDescriptor.cpp',
              dawn_dir / 'dawn/src/dawn/utils/ComboRenderBundleEncoderDescriptor.cpp',
              dawn_dir / 'dawn/src/dawn/utils/BinarySemaphore.cpp',
              dawn_dir / 'dawn/src/dawn/utils/SystemUtils.cpp',
              dawn_dir / 'dawn/src/dawn/utils/TextureUtils.cpp',
              dawn_dir / 'dawn/src/dawn/utils/WireHelper.cpp',
              dawn_dir / 'dawn/src/dawn/utils/TerribleCommandBuffer.cpp',
              dawn_dir / 'dawn/src/dawn/utils/WGPUHelpers.cpp',
              dawn_dir / 'dawn/src/dawn/samples/SampleUtils.cpp',
          ),
        # dependencies: [frastWebGpu_dep, glfw_lib])
        dependencies: [dawn_main_dep],
        cpp_args: ['-DDAWN_ENABLE_BACKEND_VULKAN=1', '-DNDEBUG', '-fno-exceptions', '-fno-rtti']
        )
      '''
      wasmHello = executable(
        'wasmHello',
        files('./apps/basic.cc'),
        )
    endif


  endif
endif
