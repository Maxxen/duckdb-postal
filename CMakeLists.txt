cmake_minimum_required(VERSION 2.8.12)

# Set extension name here
set(TARGET_NAME postal)


set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})



# Build the dependencies
# We do this by invoking a new cmake process to ensure they are built before
# the rest of this cmake project is processed
execute_process(
  # Generate project
  COMMAND
    ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR}
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DOSX_BUILD_ARCH=${OSX_BUILD_ARCH}
    -DVCPKG_MANIFEST_DIR='${VCPKG_MANIFEST_DIR}'
    -DVCPKG_INSTALLED_DIR='${CMAKE_BINARY_DIR}/vcpkg_installed'
    -DCMAKE_TOOLCHAIN_FILE='${CMAKE_TOOLCHAIN_FILE}'
    -DVCPKG_TARGET_TRIPLET='${VCPKG_TARGET_TRIPLET}'
    -S ${CMAKE_CURRENT_SOURCE_DIR}/third_party -B ${CMAKE_BINARY_DIR}/third_party
  RESULT_VARIABLE DEPENDENCIES_GENERATE_RESULT)
if(NOT DEPENDENCIES_GENERATE_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not generate dependencies project")
endif()
execute_process(
  # Build project
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/third_party --config
          ${CMAKE_BUILD_TYPE} RESULT_VARIABLE DEPENDENCIES_BUILD_RESULT)
if(NOT DEPENDENCIES_BUILD_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not build dependencies project")
endif()

set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${CMAKE_BINARY_DIR}/third_party/local)
message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")

find_library(LIBPOSTAL postal REQUIRED)
set(EXTENSION_DEPENDENCIES ${EXTENSION_DEPENDENCIES} ${LIBPOSTAL})

include_directories(src/include)
include_directories(${CMAKE_BINARY_DIR}/third_party/local/include)

set(EXTENSION_SOURCES src/postal_extension.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

target_link_libraries(${EXTENSION_NAME} ${EXTENSION_DEPENDENCIES})
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${EXTENSION_DEPENDENCIES})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
