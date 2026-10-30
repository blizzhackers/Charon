# Builds the vendored LuaJIT as a static x86 library without a developer shell.
# Mirrors upstream src/msvcbuild.bat: minilua runs DynASM, buildvm emits the VM
# object and generated headers, then the core is compiled into luajit_static.

set(LUAJIT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/upstream/src")
set(LUAJIT_DYNASM "${CMAKE_CURRENT_SOURCE_DIR}/upstream/dynasm")
set(LUAJIT_GEN "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${LUAJIT_GEN}")

# LuaJIT is always optimized; Charon's no-optimization rule covers its own hook code only.
set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELEASE "")
set(LUAJIT_CFLAGS /nologo /O2 /W3 /arch:SSE2 /Zi /D_CRT_SECURE_NO_DEPRECATE)

set(LUAJIT_LIB_FILES
    lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c
    lib_package.c lib_debug.c lib_jit.c lib_ffi.c lib_buffer.c)
list(TRANSFORM LUAJIT_LIB_FILES PREPEND "${LUAJIT_SRC}/" OUTPUT_VARIABLE LUAJIT_LIB_PATHS)

add_executable(luajit_minilua "${LUAJIT_SRC}/host/minilua.c")
target_compile_options(luajit_minilua PRIVATE ${LUAJIT_CFLAGS})

add_custom_command(
    OUTPUT "${LUAJIT_GEN}/buildvm_arch.h"
    COMMAND luajit_minilua "${LUAJIT_DYNASM}/dynasm.lua" -LN
        -D WIN -D JIT -D FFI -D ENDIAN_LE -D FPU
        -o "${LUAJIT_GEN}/buildvm_arch.h" "${LUAJIT_SRC}/vm_x86.dasc"
    DEPENDS luajit_minilua "${LUAJIT_SRC}/vm_x86.dasc"
    VERBATIM)

add_custom_command(
    OUTPUT "${LUAJIT_GEN}/luajit.h"
    COMMAND luajit_minilua "${LUAJIT_SRC}/host/genversion.lua"
        "${LUAJIT_SRC}/luajit_rolling.h" "${CMAKE_CURRENT_SOURCE_DIR}/upstream/.relver"
        "${LUAJIT_GEN}/luajit.h"
    DEPENDS luajit_minilua "${LUAJIT_SRC}/luajit_rolling.h" "${CMAKE_CURRENT_SOURCE_DIR}/upstream/.relver"
    VERBATIM)

file(GLOB LUAJIT_BUILDVM_SOURCES "${LUAJIT_SRC}/host/buildvm*.c")
add_executable(luajit_buildvm ${LUAJIT_BUILDVM_SOURCES} "${LUAJIT_GEN}/buildvm_arch.h" "${LUAJIT_GEN}/luajit.h")
target_include_directories(luajit_buildvm PRIVATE "${LUAJIT_GEN}" "${LUAJIT_SRC}" "${LUAJIT_DYNASM}")
target_compile_options(luajit_buildvm PRIVATE ${LUAJIT_CFLAGS})

set(LUAJIT_BUILDVM_OUTPUTS)
foreach(mode bcdef ffdef libdef recdef)
    add_custom_command(
        OUTPUT "${LUAJIT_GEN}/lj_${mode}.h"
        COMMAND luajit_buildvm -m ${mode} -o "${LUAJIT_GEN}/lj_${mode}.h" ${LUAJIT_LIB_PATHS}
        DEPENDS luajit_buildvm ${LUAJIT_LIB_PATHS}
        VERBATIM)
    list(APPEND LUAJIT_BUILDVM_OUTPUTS "${LUAJIT_GEN}/lj_${mode}.h")
endforeach()
add_custom_command(
    OUTPUT "${LUAJIT_GEN}/lj_folddef.h"
    COMMAND luajit_buildvm -m folddef -o "${LUAJIT_GEN}/lj_folddef.h" "${LUAJIT_SRC}/lj_opt_fold.c"
    DEPENDS luajit_buildvm "${LUAJIT_SRC}/lj_opt_fold.c"
    VERBATIM)
add_custom_command(
    OUTPUT "${LUAJIT_GEN}/lj_vm.obj"
    COMMAND luajit_buildvm -m peobj -o "${LUAJIT_GEN}/lj_vm.obj"
    DEPENDS luajit_buildvm
    VERBATIM)
list(APPEND LUAJIT_BUILDVM_OUTPUTS "${LUAJIT_GEN}/lj_folddef.h" "${LUAJIT_GEN}/lj_vm.obj")

file(GLOB LUAJIT_CORE_SOURCES "${LUAJIT_SRC}/lj_*.c" "${LUAJIT_SRC}/lib_*.c")
add_library(luajit_static STATIC ${LUAJIT_CORE_SOURCES} ${LUAJIT_BUILDVM_OUTPUTS} "${LUAJIT_GEN}/luajit.h")
set_source_files_properties("${LUAJIT_GEN}/lj_vm.obj" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
target_include_directories(luajit_static PRIVATE "${LUAJIT_GEN}")
target_include_directories(luajit_static PUBLIC "${LUAJIT_SRC}" "${LUAJIT_GEN}")
target_compile_options(luajit_static PRIVATE ${LUAJIT_CFLAGS})
# Match the CRT of the DLL that links it.
set_target_properties(luajit_static PROPERTIES MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>")
