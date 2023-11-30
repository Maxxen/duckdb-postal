cmake_minimum_required(VERSION 2.8.12)

# Set extension name here
set(TARGET_NAME postal)


set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

set(EXTENSION_SOURCES src/postal_extension.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Build the dependencies
include(ExternalProject)
ExternalProject_Add(
    LIBPOSTAL
    URL ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpostal-master.zip
    CONFIGURE_COMMAND ./bootstrap.sh && ./configure --datadir=${CMAKE_CURRENT_BINARY_DIR}/libpostal-data --disable-sse2 --prefix=${CMAKE_CURRENT_BINARY_DIR}/libpostal
    BUILD_COMMAND make -j4
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND make install
)
add_library(libpostal STATIC IMPORTED GLOBAL)
add_dependencies(libpostal LIBPOSTAL)
set_target_properties(libpostal PROPERTIES 
  IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/libpostal/lib/libpostal.a
  INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR}/libpostal/include
)

add_dependencies(${EXTENSION_NAME} libpostal)
add_dependencies(${LOADABLE_EXTENSION_NAME} libpostal)

#ADD_LIBRARY(postal STATIC IMPORTED GLOBAL)

# Link OpenSSL in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} libpostal)
target_link_libraries(${LOADABLE_EXTENSION_NAME} libpostal)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
