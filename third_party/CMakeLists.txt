cmake_minimum_required(VERSION 2.8.12)
project(dependencies_build)

set(LOCAL_INSTALL_DIR ${CMAKE_BINARY_DIR}/local)
set_directory_properties(PROPERTIES EP_BASE ${CMAKE_BINARY_DIR}/projects)

include(ExternalProject)
ExternalProject_Add(
    LIBPOSTAL
    URL ${CMAKE_CURRENT_SOURCE_DIR}/libpostal-master.zip
    CONFIGURE_COMMAND ./bootstrap.sh && ./configure --datadir=${CMAKE_CURRENT_BINARY_DIR}/libpostal-data --disable-sse2 --prefix=${LOCAL_INSTALL_DIR}
    BUILD_COMMAND make -j4
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND make install
)