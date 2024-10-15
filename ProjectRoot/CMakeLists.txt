cmake_minimum_required(VERSION 3.5.1)
project(OTAC)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

### Configure Compiler ########################################################
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" AND ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    set(CMAKE_EXE_LINKER_FLAGS "-lthr ${CMAKE_EXE_LINKER_FLAGS}")
    set(CMAKE_CXX_FLAGS "-stdlib=libc++ ${CMAKE_CXX_FLAGS}")
endif()

### Set up build environment ##################################################
# Choose a static or shared-library build (shared is default, and static will
# probably need some special care!)
# Set this to ON in order to link a static build of UHD:
option(UHD_USE_STATIC_LIBS OFF)

# To add UHD as a dependency to this project, add a line such as this:
find_package(Boost REQUIRED COMPONENTS chrono)
find_package(UHD 4.2.0 REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(PahoMqttCpp REQUIRED)
pkg_search_module(FFTW REQUIRED fftw3 IMPORTED_TARGET)
# find_package(FFTW3 REQUIRED)
# find_package(Curses REQUIRED)
# The version in  ^^^^^  here is a minimum version.
# To specify an exact version:
#find_package(UHD 4.0.0 EXACT REQUIRED)

find_library(
    FFTW_DOUBLE_THREADS_LIB
    NAMES "fftw3_threads"
    PATHS ${PKG_FFTW_LIBRARY_DIRS} ${LIB_INSTALL_DIR}
)
          
if (FFTW_DOUBLE_THREADS_LIB)
    set(FFTW_DOUBLE_THREADS_LIB_FOUND TRUE)
    set(FFTW_LIBRARIES ${FFTW_LIBRARIES} ${FFTW_DOUBLE_THREADS_LIB})
    add_library(FFTW::DoubleThreads INTERFACE IMPORTED)
    set_target_properties(FFTW::DoubleThreads
        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${FFTW_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${FFTW_DOUBLE_THREADS_LIB}"
    )
else()
    set(FFTW_DOUBLE_THREADS_LIB_FOUND FALSE)
endif()

# This example also requires Boost.
# Set components here, then include UHDBoost to do the actual finding
set(UHD_BOOST_REQUIRED_COMPONENTS
    program_options
    system
    thread
)
set(BOOST_MIN_VERSION 1.65)
include(UHDBoost)

# Recursively find all header files in the include directory
file(GLOB_RECURSE HEADER_FILES ${CMAKE_SOURCE_DIR}/include/*.hpp)

# Get unique directories containing header files
set(INCLUDE_DIRS)
foreach(_headerFile ${HEADER_FILES})
    get_filename_component(_dir ${_headerFile} PATH)
    list(APPEND INCLUDE_DIRS ${_dir})
endforeach()
list(REMOVE_DUPLICATES INCLUDE_DIRS)

# need these include and link directories for the build
include_directories(
    ${Boost_INCLUDE_DIRS}
    ${UHD_INCLUDE_DIRS}
    ${INCLUDE_DIRS}
    # ${FFTW3_INCLUDE_DIR}
)
link_directories(${Boost_LIBRARY_DIRS})

set(PCH_HEADER "${CMAKE_SOURCE_DIR}/include/pch.hpp")

# Function to enable precompiled headers
function(enable_precompiled_headers target)
    target_precompile_headers(${target} PRIVATE "${PCH_HEADER}")
endfunction()

# Add libraries
# add_library(lib_config STATIC src/lib_config/ConfigParser.cpp)
# enable_precompiled_headers(lib_config)
# add_library(lib_waveform STATIC src/lib_waveform/Waveforms.cpp)
# enable_precompiled_headers(lib_waveform)
# add_library(lib_utils STATIC src/lib_utils/Utility.cpp)
# enable_precompiled_headers(lib_utils)
# add_library(lib_usrp STATIC src/lib_usrp/USRPclass.cpp)
# enable_precompiled_headers(lib_usrp)

SET(executables_list rx_with_cb tx_zfc)
# SET(files_list main/centralized_otac.cpp main/analysis/rx_to_file.cpp main/analysis/tx_zfc.cpp)
SET(files_list main/analysis/rx_with_callback.cpp main/analysis/tx_zfc.cpp)
SET(counter 0)

### Make the executable #######################################################
foreach(executable ${executables_list})
    list(GET files_list ${counter} file_loc)
    # add_executable(${executable} ${file_loc} src/lib_log/logger.cpp src/lib_csd/cyclestartdetector.cpp src/lib_csd/peakdetector.cpp src/lib_fft/FFTWrapper.cpp src/lib_config/config_parser.cpp src/lib_waveform/waveforms.cpp src/lib_utils/utility.cpp src/lib_usrp/usrp_class.cpp src/lib_mqtt/MQTTClient.cpp src/lib_cal/calibration.cpp src/lib_otac/otac_processor.cpp)
    add_executable(${executable} ${file_loc} src/lib_log/logger.cpp src/lib_config/config_parser.cpp src/lib_waveform/waveforms.cpp src/lib_utils/utility.cpp src/lib_usrp/usrp_class.cpp src/lib_mqtt/MQTTClient.cpp)
    target_link_libraries(${executable} ${UHD_LIBRARIES} ${Boost_LIBRARIES} PahoMqttCpp::paho-mqttpp3)
    # target_link_libraries(${executable} ${UHD_LIBRARIES} ${Boost_LIBRARIES} PahoMqttCpp::paho-mqttpp3 PkgConfig::FFTW ${FFTW_DOUBLE_THREADS_LIB})
    math(EXPR counter "${counter} + 1")
    enable_precompiled_headers(${executable})
endforeach()

set(CMAKE_BUILD_TYPE "Debug")

# Shared library case: All we need to do is link against the library, and
# anything else we need (in this case, some Boost libraries):
# if(NOT UHD_USE_STATIC_LIBS)
#     message(STATUS "Linking against shared UHD library.")
#     target_link_libraries(centralized_arch_leaf ${UHD_LIBRARIES} ${Boost_LIBRARIES} ${CURSES_LIBRARIES})
#     # Shared library case: All we need to do is link against the library, and
# # anything else we need (in this case, some Boost libraries):
# else(NOT UHD_USE_STATIC_LIBS)
#     message(STATUS "Linking against static UHD library.")
#     target_link_libraries(centralized_arch_leaf ${UHD_STATIC_LIB_LINK_FLAG} ${UHD_STATIC_LIB_DEPS} ${Boost_LIBRARIES} ${CURSES_LIBRARIES})
# endif(NOT UHD_USE_STATIC_LIBS)

# target_link_libraries(centralized_arch_leaf ${UHD_LIBRARIES} ${Boost_LIBRARIES} ${CURSES_LIBRARIES} lib_config lib_waveform lib_utils lib_usrp)
# target_link_libraries(centralized_arch_leaf ${UHD_LIBRARIES} ${Boost_LIBRARIES})

### Once it's built... ########################################################
# Here, you would have commands to install your program.
# We will skip these in this example.
