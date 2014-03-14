set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# Arnold
find_package(Arnold 4.0 REQUIRED)
include_directories(${ARNOLD_INCLUDE_DIR})

# Boost
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost 1.44 REQUIRED COMPONENTS regex thread system)
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})


add_library(rvdriver SHARED rvdriver.cpp)

set_target_properties(rvdriver PROPERTIES PREFIX "")
add_definitions(-Wall -Wsign-compare -fvisibility=hidden -fPIC)
set(CMAKE_EXE_LINKER_FLAGS --whole-archive -Bsymbolic)

target_link_libraries(rvdriver ${ARNOLD_LIBRARY} boost_thread boost_system)

install(TARGETS rvdriver
    LIBRARY DESTINATION plugins)