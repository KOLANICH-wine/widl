cmake_minimum_required(VERSION 3.18.0)
project(widl)
include(GNUInstallDirs)

set(CPACK_PACKAGE_NAME "widl")
set(CPACK_PACKAGE_VENDOR "Wine, MinGW-w64 and ReactOS developers")
set(CPACK_PACKAGE_VERSION_MAJOR "5")
set(CPACK_PACKAGE_VERSION_MINOR "18")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION "Wine IDL compiler")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/KOLANICH/widl")
set(CPACK_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_MAINTAINER}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/ReadMe.md")
set(CPACK_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_MAINTAINER}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/ReadMe.md")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_NSIS_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_ENABLE_COMPONENT_DEPENDS ON)
set(CPACK_RPM_ENABLE_COMPONENT_DEPENDS ON)
set(CPACK_DEBIAN_COMPRESSION_TYPE "xz")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS_POLICY ">=")

set(source_dir "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(private_include_dir "${CMAKE_CURRENT_SOURCE_DIR}/private_include")
set(include_dir "${CMAKE_CURRENT_SOURCE_DIR}/include")

file(RELATIVE_PATH BIN_TO_INCLUDEDIR "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
file(TO_NATIVE_PATH "${BIN_TO_INCLUDEDIR}" BIN_TO_INCLUDEDIR)

configure_file("${source_dir}/config.h.in" "${private_include_dir}/config.h")

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

find_package(WinePort REQUIRED)
find_package(libwpp REQUIRED)

set(lexerSrc "${source_dir}/parser.yy.c")
FLEX_TARGET(widl_lexer "${source_dir}/parser.l" "${lexerSrc}")

set(parserSrc "${source_dir}/parser.tab.c")
BISON_TARGET(widl_parser "${source_dir}/parser.y" "${parserSrc}" COMPILE_FLAGS "--name-prefix=ppy_")

ADD_FLEX_BISON_DEPENDENCY(widl_lexer widl_parser)

file(GLOB_RECURSE SOURCE "${source_dir}/*.c" "${source_dir}/*.cpp")




if(REACTOS)
	add_host_tool(widl "${SOURCE}")
	target_link_libraries(widl PRIVATE host_includes wpphost)
else()
	add_executable(widl "${SOURCE}")
	target_link_libraries(widl PRIVATE "${WinePort_LINK_LIBRARIES}" "${libwpp_LINK_LIBRARIES}")
endif()
# Taken from widl.rbuild
target_compile_definitions(widl PRIVATE -DINT16=SHORT)
target_include_directories(widl PRIVATE "${WinePort_INCLUDE_DIRS}" "${libwpp_INCLUDE_DIRS}" "${source_dir}" "${private_include_dir}" "${include_dir}")

if(MSVC)
    if(MSVC_VERSION LESS 1900)
        target_compile_definitions(widl PRIVATE "-Dsnprintf=_snprintf")

        # Add this definition for WDK only, VS 9 doesn't like that
        if(DEFINED ENV{DDKBUILDENV})
            target_compile_definitions("-Dvsnprintf=_vsnprintf")
        endif()
    endif()
    # Disable warning "'>': signed/unsigned mismatch"
    target_compile_options(widl PRIVATE "/wd4018")

    # Disable warning "unary minus operator applied to unsigned type, result still unsigned"
    target_compile_options(widl PRIVATE "/wd4146")

    # Disable warning "'=': conversion from 'a' to 'b', possible loss of data"
    target_compile_options(widl PRIVATE "/wd4244")
endif()

