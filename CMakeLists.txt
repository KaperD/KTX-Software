# Copyright 2015-2023 The Khronos Group Inc.
# Copyright 2022-2023 RasterGrid Kft.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.15)

project(KTX-Software
    DESCRIPTION "Libraries and tools to create and read KTX image texture files."
)

set(LIB_TYPE STATIC)

set(KTX_MAIN_SRC
    include/KHR/khr_df.h
    include/ktx.h
    lib/basis_sgd.h
    lib/basis_transcode.cpp
    lib/miniz_wrapper.cpp
    lib/basisu/transcoder/basisu_containers.h
    lib/basisu/transcoder/basisu_containers_impl.h
    lib/basisu/transcoder/basisu_file_headers.h
    lib/basisu/transcoder/basisu_transcoder_internal.h
    lib/basisu/transcoder/basisu_transcoder_uastc.h
    lib/basisu/transcoder/basisu_transcoder.cpp
    lib/basisu/transcoder/basisu_transcoder.h
    lib/basisu/transcoder/basisu.h
    lib/basisu/zstd/zstd.c
    lib/checkheader.c
    lib/dfdutils/createdfd.c
    lib/dfdutils/colourspaces.c
    lib/dfdutils/dfd.h
    lib/dfdutils/interpretdfd.c
    lib/dfdutils/printdfd.c
    lib/dfdutils/queries.c
    lib/dfdutils/vk2dfd.c
    lib/dfdutils/vk2dfd.inl
    lib/dfdutils/vulkan/vk_platform.h
    lib/dfdutils/vulkan/vulkan_core.h
    lib/filestream.c
    lib/filestream.h
    lib/formatsize.h
    lib/gl_format.h
    lib/hashlist.c
    lib/info.c
    lib/ktxint.h
    lib/memstream.c
    lib/memstream.h
    lib/strings.c
    lib/swap.c
    lib/texture.c
    lib/texture.h
    lib/texture2.c
    lib/texture2.h
    lib/uthash.h
    lib/vk_format.h
    lib/vkformat_check.c
    lib/vkformat_enum.h
    lib/vkformat_str.c
    lib/vkformat_typesize.c
    )

# Read-only library
add_library( ktx_read ${LIB_TYPE}
    ${KTX_MAIN_SRC}
    )

macro(common_libktx_settings target enable_write library_type)

    if(TARGET mkvk)
        # Creating vulkan headers only required after Vulkan Spec/SDK updates.
        add_dependencies(${target} mkvk)
    endif()

    set_target_properties(${target} PROPERTIES
        PUBLIC_HEADER
#          "${CMAKE_CURRENT_SOURCE_DIR}/include/ktx.h;${CMAKE_CURRENT_SOURCE_DIR}/include/KHR/khr_df.h"
          # Omit khr_df.h. Its installation has to be handled separately to
          # workaround CMake's failure to preserve the directory hierarchy.
          "${CMAKE_CURRENT_SOURCE_DIR}/include/ktx.h"
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "YES"
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES

    )
    if(IOS)
        set_target_properties(${target} PROPERTIES
            FRAMEWORK TRUE
        )
    endif()

    if( NOT ${library_type} STREQUAL STATIC )
        # Must not call this macro for static libs on Windows. To keep
        # the if test simple, never call it for static libs. On macOS
        # and iOS Xcode knows libs aren't signed so it would ignore the
        # settings made by this macro.
        set_code_sign(${target} "NOPPS")
    endif()

    target_compile_definitions(
        ${target}
    PUBLIC
        "$<$<CONFIG:Debug>:_DEBUG;DEBUG>"
    PRIVATE
        LIBKTX
    )

    # C/C++ Standard
    target_compile_features(${target} PUBLIC c_std_99 cxx_std_11)

    # Compiler Warning Flags
    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE
            -Wno-nested-anon-types
            -Wno-gnu-anonymous-struct
        )
    else()
        target_compile_options(${target} PRIVATE
            # clang options
            $<$<CXX_COMPILER_ID:AppleClang,Clang>:
                -Wno-nested-anon-types
                -Wno-gnu-anonymous-struct
            >
            $<$<CXX_COMPILER_ID:GNU>:
                -Wno-cast-function-type
            >
            # not clang options
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:
                -Wno-pedantic
            >
        )
    endif()

    target_include_directories(
        ${target}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/basisu/transcoder>
        $<INSTALL_INTERFACE:lib/basisu/transcoder>

        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/basisu/zstd>
        $<INSTALL_INTERFACE:lib/basisu/zstd>

        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/utils>
        $<INSTALL_INTERFACE:utils>
    )

    target_include_directories(
        ${target}
        SYSTEM
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/other_include>
        $<INSTALL_INTERFACE:other_include>
    )

    if( ${library_type} STREQUAL STATIC )
        target_compile_definitions(${target} PUBLIC KHRONOS_STATIC)
    endif()

    # To reduce size, don't support transcoding to ancient formats.
    target_compile_definitions(${target} PRIVATE BASISD_SUPPORT_FXT1=0)

    # TODO: make options for all formats and good per-platform defaults
    # - BASISD_SUPPORT_UASTC
    # - BASISD_SUPPORT_DXT1 (BC1)
    # - BASISD_SUPPORT_DXT5A (BC3/4/5)
    # - BASISD_SUPPORT_BC7
    # - BASISD_SUPPORT_BC7_MODE5
    # - BASISD_SUPPORT_PVRTC1
    # - BASISD_SUPPORT_ETC2_EAC_A8
    # - BASISD_SUPPORT_ASTC
    # - BASISD_SUPPORT_ATC
    # - BASISD_SUPPORT_ASTC_HIGHER_OPAQUE_QUALITY
    # - BASISD_SUPPORT_ETC2_EAC_RG11
    # - BASISD_SUPPORT_FXT1
    # - BASISD_SUPPORT_PVRTC2

    if(WIN32)
        target_compile_definitions(
            ${target}
        PRIVATE
            # Only set dllexport when building a shared library.
            $<$<STREQUAL:${library_type},SHARED>:KTX_API=__declspec\(dllexport\)>
        PUBLIC # only for basisu_c_binding.
            BASISU_NO_ITERATOR_DEBUG_LEVEL
        )
        # The generator automatically sets the needed VCLinker
        # option when a .def file is seen in sources.
        # The def files that we add have a different syntax depending on the ABI
        if(MINGW)
            target_sources(
                ${target}
            PRIVATE
                lib/internalexport_mingw.def
                $<${enable_write}:lib/internalexport_write_mingw.def>
            )
            # Need these flags if mingw happens to target the ucrt (new) rather
            # than the legacy msvcrt. Otherwise tests will fail to run because
            # the necessary dlls will be missing. If we statically link
            # them instead it's fine. This does not cause any abberations if
            # the mingw toolchain targets msvcrt instead.
            target_link_options(${target} PUBLIC -static-libgcc -static-libstdc++)
        else()
            target_sources(
                ${target}
            PRIVATE
                lib/internalexport.def
                $<${enable_write}:lib/internalexport_write.def>
            )
        endif()
    elseif(EMSCRIPTEN)
        target_compile_definitions(${target} PRIVATE
            # To reduce size, don't support transcoding to formats not
            # supported # by WebGL.
            BASISD_SUPPORT_ATC=0
            BASISD_SUPPORT_PVRTC2=0
            # Don't support higher quality mode to avoid 64k table.
            BASISD_SUPPORT_ASTC_HIGHER_OPAQUE_QUALITY=0
            KTX_OMIT_VULKAN=1
        )
    endif()

    if(KTX_FEATURE_KTX1)
        target_compile_definitions(${target} PUBLIC KTX_FEATURE_KTX1)
        target_sources(
            ${target}
        PRIVATE
            lib/texture1.c
            lib/texture1.h
        )
    endif()

    if(KTX_FEATURE_KTX2)
        target_compile_definitions(${target} PUBLIC KTX_FEATURE_KTX2)
    endif()

    if(WIN32)
        if(MINGW)
            # Check if the Threads package is provided; if using Mingw it MIGHT be
            find_package(Threads)
            if(Threads_FOUND AND CMAKE_USE_PTHREADS_INIT)
                target_compile_definitions(${target} PRIVATE WIN32_HAS_PTHREADS)
                target_link_libraries(${target} PRIVATE Threads::Threads)
            endif()
        endif()

    elseif(APPLE)
        if(KTX_EMBED_BITCODE)
            target_compile_options(${target} PRIVATE "-fembed-bitcode")
        endif()

    elseif(LINUX)
        find_package(Threads REQUIRED)
        target_link_libraries(
            ${target}
            PRIVATE
            dl
            Threads::Threads
        )
    endif()

    if(KTX_FEATURE_VK_UPLOAD)
        target_sources(
            ${target}
        PRIVATE
            include/ktxvulkan.h
            lib/vk_funcs.c
            lib/vk_funcs.h
            lib/vkloader.c
        )
        target_include_directories(
            ${target}
        PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/dfdutils>
            $<INSTALL_INTERFACE:lib/dfdutils>
        )

        get_target_property( KTX_PUBLIC_HEADER ${target} PUBLIC_HEADER )
        list(APPEND KTX_PUBLIC_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/ktxvulkan.h)
        set_target_properties(${target} PROPERTIES
            PUBLIC_HEADER "${KTX_PUBLIC_HEADER}"
        )
    else()
        target_compile_definitions( ${target} PRIVATE KTX_OMIT_VULKAN=1 )
    endif()

    # Adding write capability to target ktx
    if(${enable_write})
        target_sources(
            ${target}
        PRIVATE
            lib/basis_encode.cpp
            lib/astc_encode.cpp
            ${BASISU_ENCODER_C_SRC}
            ${BASISU_ENCODER_CXX_SRC}
            lib/writer1.c
            lib/writer2.c
        )

        target_include_directories(
            ${target}
        PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/basisu>
            $<INSTALL_INTERFACE:lib/basisu>
            $<$<BOOL:${BASISU_SUPPORT_OPENCL}>:${OpenCL_INCLUDE_DIRS}>
        )
        target_compile_definitions(
            ${target}
        PUBLIC
            KTX_FEATURE_WRITE
        PRIVATE
            # BASISD_SUPPORT_KTX2 has to be 1 to compile the encoder. We
            # don't use it. Hopefully it doesn't add too much code. We're using
            # the zstd encoder in basisu by explicitly including the file in our
            # source list. We don't need the related code in the encoder.
            BASISD_SUPPORT_KTX2_ZSTD=0
            BASISD_SUPPORT_KTX2=1
            $<$<BOOL:${BASISU_SUPPORT_SSE}>:BASISU_SUPPORT_SSE=1>
            $<$<NOT:$<BOOL:${BASISU_SUPPORT_SSE}>>:BASISU_SUPPORT_SSE=0>
            $<$<BOOL:${BASISU_SUPPORT_OPENCL}>:BASISU_SUPPORT_OPENCL=1>
            $<$<NOT:$<BOOL:${BASISU_SUPPORT_OPENCL}>>:BASISU_SUPPORT_OPENCL=0>
        )
        target_compile_options(
            ${target}
        PRIVATE
            $<$<AND:$<BOOL:${BASISU_SUPPORT_SSE}>,$<CXX_COMPILER_ID:AppleClang,Clang,GNU>>:
                -msse4.1
            >
        )
        target_link_libraries(
            ${target}
        PRIVATE
            $<$<BOOL:${BASISU_SUPPORT_OPENCL}>:${OpenCL_LIBRARIES}>
        )
    endif()
endmacro(common_libktx_settings)

common_libktx_settings(ktx_read 0 ${LIB_TYPE})

target_compile_definitions(
    ktx_read
PRIVATE
    # We're reading the files ourselves so don't need Basis KTX v2 support.
    BASISD_SUPPORT_KTX2_ZSTD=0
    BASISD_SUPPORT_KTX2=0
)

# Turn off these warnings until Rich fixes the occurences.
# It it not clear to me if generator expressions can be used here
# hence the long-winded way.
#message(STATUS
#        "CMAKE_CXX_COMPILER_ID = ${CMAKE_CXX_COMPILER_ID} "
#        "CMAKE_CXX_COMPILER_VERSION = ${CMAKE_CXX_COMPILER_VERSION}"
#)
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
# Currently no need to disable any warnings in basisu code. Rich fixed them.
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(
        # It's too much work to discriminate which files need which warnings
        # disabled.
        ${BASISU_ENCODER_CXX_SRC}
        PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-variable;-Wno-class-memaccess;-Wno-misleading-indentation;-Wno-extra;-Wno-deprecated-copy;-Wno-parentheses;-Wno-strict-aliasing"
    )
    set_source_files_properties(
        lib/basisu/transcoder/basisu_transcoder.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-function;-Wno-unused-variable;-Wno-class-memaccess;-Wno-maybe-uninitialized"
    )
    if (${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER_EQUAL "11")
        if (${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS "12" )
        # Version 11 raises several stringop-overflow warnings in some
        # very hard to decipher code. They appear to be bogus based on
        # the facts that we have never seen a crash and version 12 no
        # longer raises the warnings.
        get_source_file_property(cur_options
            lib/basisu/encoder/basisu_comp.cpp
            COMPILE_OPTIONS
        )
        set_source_files_properties(
            lib/basisu/encoder/basisu_comp.cpp
            PROPERTIES COMPILE_OPTIONS "${cur_options};-Wno-stringop-overflow"
        )
        endif()
    endif()
    if (${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER_EQUAL "12.0")
        # Version 12 newly raises this warning on basisu_uastc_enc.cpp.
        # There seems no way for the index calculated by the code at
        # line 326, where the error is raised, to be > the array length.
        # Also we have never seen any crashes.
        set_source_files_properties(
            lib/basisu/encoder/basisu_uastc_enc.cpp
            PROPERTIES COMPILE_OPTIONS "-Wno-stringop-overflow"
        )
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        # Versions equivalency from https://en.wikipedia.org/wiki/Xcode#Xcode_11.x_-_14.x_(since_SwiftUI_framework)_2
        if (${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER_EQUAL "14")
            set( clang_version ${CMAKE_CXX_COMPILER_VERSION})
        elseif (${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER_EQUAL "13.1.0")
            set( clang_version "13.0.0")
        elseif (${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER_EQUAL "13.0.0")
            set( clang_version "12.0.0")
        elseif (${CMAKE_CXX_COMPILER_VERSION} VERSION_GREATER_EQUAL "12.0.5")
            set( clang_version "11.1.0")
        elseif (${CMAKE_CXX_COMPILER_VERSION} VERSION_EQUAL "12.0.0")
            set( clang_version "10.0.0")
        else()
            message(FATAL_ERROR "Unsupported AppleClang version")
        endif()
    else()
        set( clang_version ${CMAKE_CXX_COMPILER_VERSION} )
    endif()
    # BEWARE: set_source_files_properties is not additive; it replaces.
    if (${clang_version} VERSION_GREATER_EQUAL "12.0.0")
        set_source_files_properties( lib/basisu/encoder/basisu_kernels_sse.cpp
            PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-deprecated-copy;-Wno-uninitialized-const-reference"
        )
    else()
        set_source_files_properties( lib/basisu/encoder/basisu_kernels_sse.cpp
            PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter"
        )
    endif()
    if (${clang_version} VERSION_GREATER_EQUAL "14.0")
        # These are for Emscripten which is ahead of xcode in its clang
        # version. Also future proofing for when xcode catches up.
        set_source_files_properties(
            ${BASISU_ENCODER_CXX_SRC}
            PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-variable;-Wno-unused-parameter;-Wno-deprecated-copy-with-user-provided-copy"
        )
        set_source_files_properties(
            lib/basisu/transcoder/basisu_transcoder.cpp
            PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare;-Wno-unused-function;-Wno-unused-variable"
        )
        set_source_files_properties(
            lib/basisu/zstd/zstd.c
            PROPERTIES COMPILE_OPTIONS "-Wno-unused-function"
        )
    endif()
else()
    message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} not yet supported.")
endif()

# Use of this to install KHR/khr_df.h is due to CMake's failure to
# preserve the include source folder hierarchy.
# See https://gitlab.kitware.com/cmake/cmake/-/issues/16739.
if (IOS)
    set_source_files_properties(
          include/KHR/khr_df.h
          PROPERTIES MACOSX_PACKAGE_LOCATION Headers/KHR
    )
else()
    include(GNUInstallDirs)
    install(FILES include/KHR/khr_df.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/KHR
        COMPONENT dev
    )
endif()

# vim:ai:ts=4:sts=2:sw=2:expandtab
