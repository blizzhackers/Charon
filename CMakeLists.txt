cmake_minimum_required(VERSION 3.15.0 FATAL_ERROR)

set(CMAKE_SYSTEM_VERSION 10.0 CACHE STRING "" FORCE)

project(Charon CXX)


################################################################################
# Set target arch type if empty. Visual studio solution generator provides it.
################################################################################
if(NOT CMAKE_VS_PLATFORM_NAME)
    set(CMAKE_VS_PLATFORM_NAME "x86")
endif()

message("${CMAKE_VS_PLATFORM_NAME} architecture in use")

if(NOT ("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x86" OR "${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32"))
    message(FATAL_ERROR "${CMAKE_VS_PLATFORM_NAME} arch is not supported!")
endif()

################################################################################
# Global configuration types
################################################################################
set(CMAKE_CONFIGURATION_TYPES
    "Debug"
    "Release"
    CACHE STRING "" FORCE
)

################################################################################
# Global compiler options
################################################################################
if(MSVC)
    # remove default flags provided with CMake for MSVC
    set(CMAKE_CXX_FLAGS "")
    set(CMAKE_CXX_FLAGS_DEBUG "")
    set(CMAKE_CXX_FLAGS_RELEASE "")
endif()

################################################################################
# Global linker options
################################################################################
if(MSVC)
    # remove default flags provided with CMake for MSVC
    set(CMAKE_EXE_LINKER_FLAGS "")
    set(CMAKE_MODULE_LINKER_FLAGS "")
    set(CMAKE_SHARED_LINKER_FLAGS "")
    set(CMAKE_STATIC_LINKER_FLAGS "")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "${CMAKE_MODULE_LINKER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_STATIC_LINKER_FLAGS_DEBUG "${CMAKE_STATIC_LINKER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS}")
endif()

################################################################################
# Nuget packages function stub.
################################################################################
function(use_package TARGET PACKAGE VERSION)
    message(WARNING "No implementation of use_package. Create yours. "
                    "Package \"${PACKAGE}\" with version \"${VERSION}\" "
                    "for target \"${TARGET}\" is ignored!")
endfunction()

################################################################################
# Common utils
################################################################################
include(CMake/Utils.cmake)

################################################################################
# Additional Global Settings(add specific info there)
################################################################################
include(CMake/GlobalSettingsInclude.cmake OPTIONAL)

################################################################################
# Use solution folders feature
################################################################################
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

################################################################################
# Sub-projects
################################################################################



























set(PROJECT_NAME Charon)

################################################################################
# Source groups
################################################################################
#set(no_group_source_files
#    "features/*.cpp"
#    "framework/*.cpp"
#    "headers/*.h"
#)

file(GLOB_RECURSE no_group_source_files RELATIVE ${CMAKE_SOURCE_DIR} "features/*.cpp" "framework/*.cpp")
source_group("" FILES ${no_group_source_files})

set(Source_Files
    "dllmain.cpp"
)
source_group("Source Files" FILES ${Source_Files})

set(ALL_FILES
    ${no_group_source_files}
    ${Source_Files}
)

################################################################################
# Target
################################################################################
add_library(${PROJECT_NAME} SHARED ${ALL_FILES})
target_link_libraries(${PROJECT_NAME} Crypt32.lib)

use_props(${PROJECT_NAME} "${CMAKE_CONFIGURATION_TYPES}" "${DEFAULT_CXX_PROPS}")
set(ROOT_NAMESPACE Charon)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_GLOBAL_KEYWORD "Win32Proj"
)
# Game callbacks include assembly and indirect entry points the optimizer cannot infer.
set_target_properties(${PROJECT_NAME} PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE "FALSE"
)
################################################################################
# MSVC runtime library
################################################################################
get_property(MSVC_RUNTIME_LIBRARY_DEFAULT TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY)
string(CONCAT "MSVC_RUNTIME_LIBRARY_STR"
    $<$<CONFIG:Debug>:
        MultiThreadedDebug
    >
    $<$<CONFIG:Release>:
        MultiThreaded
    >
    $<$<NOT:$<OR:$<CONFIG:Debug>,$<CONFIG:Release>>>:${MSVC_RUNTIME_LIBRARY_DEFAULT}>
)
set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY ${MSVC_RUNTIME_LIBRARY_STR})

################################################################################
# Include directories
################################################################################
target_include_directories(${PROJECT_NAME} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/."
)

################################################################################
# Compile definitions
################################################################################
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:"
        "_DEBUG"
    ">"
    "$<$<CONFIG:Release>:"
        "NDEBUG"
    ">"
    "WIN32;"
    "CHARON_EXPORTS;"
    "_WINDOWS;"
    "_USRDLL;"
    "UNICODE;"
    "_UNICODE"
)

################################################################################
# Compile and link options
################################################################################
if(MSVC)
    # Keep production Release unoptimized, including linker dead-code removal.
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:
            /Gm-
        >
        $<$<CONFIG:Release>:
            /Od;
            /Ob0;
            /Oi-;
            /Oy-;
            /GL-
        >
        /permissive-;
        /std:c++17;
        /sdl;
        /W4;
        ${DEFAULT_CXX_DEBUG_INFORMATION_FORMAT};
        /FAcs;
        ${DEFAULT_CXX_EXCEPTION_HANDLING};
        /Y-
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:
            /INCREMENTAL
        >
        $<$<CONFIG:Release>:
            /OPT:NOREF;
            /OPT:NOICF;
            /INCREMENTAL:NO
        >
        /DEBUG;
        /SUBSYSTEM:WINDOWS
    )
endif()

option(CHARON_BUILD_OPENGL_TESTS "Build synthetic native renderer checks (GPU required)" OFF)
if(CHARON_BUILD_OPENGL_TESTS)
    enable_testing()
    add_executable(charon_opengl_device_test tests/opengl_device.cpp framework/opengl_device.cpp)
    add_executable(charon_opengl_scalefx_test tests/opengl_scalefx.cpp framework/opengl_device.cpp)
    add_executable(charon_opengl_lcd_test tests/opengl_lcd.cpp framework/opengl_device.cpp)
    target_compile_definitions(charon_opengl_lcd_test PRIVATE CHARON_OPENGL_TEST_CLOCK)
    add_executable(charon_opengl_sprite_test tests/opengl_sprite_decode.cpp)
    add_executable(charon_opengl_tile_test tests/opengl_tile_decode.cpp)
    add_executable(charon_opengl_window_test tests/opengl_window.cpp framework/opengl_window.cpp framework/opengl_device.cpp)
    add_executable(charon_opengl_sprite_cache_test tests/opengl_sprite_cache.cpp)
    add_executable(charon_opengl_movie_message_test tests/opengl_movie_messages.cpp)
    target_compile_definitions(charon_opengl_device_test PRIVATE CHARON_OPENGL_TEST_CONTEXT)
    foreach(renderer_test charon_opengl_device_test charon_opengl_scalefx_test charon_opengl_lcd_test charon_opengl_sprite_test charon_opengl_tile_test charon_opengl_window_test charon_opengl_sprite_cache_test charon_opengl_movie_message_test)
        target_include_directories(${renderer_test} PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/headers")
        target_compile_features(${renderer_test} PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(${renderer_test} PRIVATE /W4 /EHsc /UNDEBUG
                $<$<CONFIG:Debug>:/Od> $<$<CONFIG:Debug>:/RTC1> $<$<CONFIG:Release>:/O2>)
        endif()
        add_test(NAME ${renderer_test} COMMAND ${renderer_test})
        set_tests_properties(${renderer_test} PROPERTIES TIMEOUT 45)
    endforeach()
    set(CHARON_ORIGINAL_GAME "" CACHE FILEPATH "Exact original Game.exe for read-only decoder comparison")
    if(CHARON_ORIGINAL_GAME)
        add_executable(charon_opengl_original_test tests/opengl_original_decode.cpp)
        target_include_directories(charon_opengl_original_test PRIVATE "${CMAKE_SOURCE_DIR}")
        target_compile_features(charon_opengl_original_test PRIVATE cxx_std_17)
        target_compile_options(charon_opengl_original_test PRIVATE /W4 /EHsc /UNDEBUG)
        add_test(NAME charon_opengl_original_test COMMAND charon_opengl_original_test "${CHARON_ORIGINAL_GAME}")
        set_tests_properties(charon_opengl_original_test PROPERTIES TIMEOUT 45)
    endif()
endif()

# Tests are opt-in for local use; CI intentionally builds the DLL only.
option(CHARON_BUILD_LOCAL_TESTS "Build local gameplay and patch regression checks" OFF)
if(CHARON_BUILD_LOCAL_TESTS)
    enable_testing()
    add_executable(charon_settings_experience_test tests/settings_experience.cpp)
    add_executable(charon_custom_packet_test tests/custom_packet.cpp)
    add_executable(charon_hooks_test tests/memory_patch.cpp framework/hook.cpp)
    add_executable(charon_experience_abi_test tests/experience_abi.cpp)
    foreach(local_test charon_settings_experience_test charon_custom_packet_test charon_hooks_test charon_experience_abi_test)
        target_include_directories(${local_test} PRIVATE "${CMAKE_SOURCE_DIR}")
        target_compile_features(${local_test} PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(${local_test} PRIVATE /W4 /EHsc /UNDEBUG /Od
                $<$<CONFIG:Debug>:/RTC1>)
        endif()
        add_test(NAME ${local_test} COMMAND ${local_test})
        set_tests_properties(${local_test} PROPERTIES TIMEOUT 45)
    endforeach()
endif()

