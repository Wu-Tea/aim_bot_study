"""Build DEV fixtures against one consistent clean header/source tree."""
import pathlib
import subprocess
import sys

dev = pathlib.Path(__file__).resolve().parents[2]
product = pathlib.Path(sys.argv[1]).resolve()
dest = dev / 'runs/clean-incident-fix-20261016/suites'
source = dest / 'source'
for root in (dev, product):
    paths = subprocess.check_output(['git', '-C', str(root), 'ls-files', 'native'], text=True).splitlines()
    for name in paths:
        p = root / name
        if p.suffix not in ('.h', '.cpp', '.inc'):
            continue
        target = source / name.removeprefix('native/')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(p.read_bytes())
# DEV fixtures now directly own the separate identity/prediction assertions.
cm = (dev / 'native/cmake/ProductTests.cmake').read_text(encoding='utf-8')
files = cm.split('add_executable(cod_native_base_tests', 1)[1].split(')', 1)[0].split()
lines = ['cmake_minimum_required(VERSION 3.26)', 'project(clean_suites LANGUAGES CXX)', 'set(CMAKE_CXX_STANDARD 17)',
         'set(CUDAToolkit_ROOT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1")', 'find_package(CUDAToolkit REQUIRED)',
         'add_executable(base_checks ' + ' '.join('"source/' + f + '"' for f in files) + ')']
(dest / 'fire_main.cpp').write_text('#include "test_support/native_test_registry.h"\nvoid register_recoil_reducer_tests(native_test::Registry&);\nvoid register_auto_fire_gate_tests(native_test::Registry&);\nint main(int argc,char** argv){native_test::Registry r;register_recoil_reducer_tests(r);register_auto_fire_gate_tests(r);return native_test::run(r,argc,argv,"Fire");}\n')
lines += ['add_executable(fire_checks fire_main.cpp source/controller_native/recoil_reducer_tests.cpp source/controller_native/auto_fire_gate_tests.cpp)',
          'foreach(target base_checks fire_checks)',
          f'target_include_directories(${{target}} PRIVATE "{product.as_posix()}/native/build/generated" "${{CMAKE_CURRENT_SOURCE_DIR}}/source" "${{CMAKE_CURRENT_SOURCE_DIR}}/source/controller_native" "${{CMAKE_CURRENT_SOURCE_DIR}}/source/vision_native/include" "${{CMAKE_CURRENT_SOURCE_DIR}}/source/vision_native/src")',
          'target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)', 'target_compile_options(${target} PRIVATE /utf-8)',
          f'target_link_libraries(${{target}} PRIVATE "{product.as_posix()}/native/build/Release/controller_native_core.lib" "{product.as_posix()}/native/build/vision_native/Release/vision_native_core.lib" CUDA::cudart "D:/env/TensorRT-10.15.1.29/lib/nvinfer_10.lib" "D:/env/TensorRT-10.15.1.29/lib/nvinfer_plugin_10.lib" d3d11 dxgi gdi32 winmm windowsapp)',
          'endforeach()']
(dest / 'CMakeLists.txt').write_text('\n'.join(lines), encoding='utf-8')
print(dest)
