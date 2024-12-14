cmake_minimum_required(VERSION 3.20)
project(phoenix CXX)

find_package(OpenSSL REQUIRED)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native -mtune=native -pthread -fopenmp -O3 -flto=auto -finline-functions -fno-rtti")

include(FetchContent)

set(BOOST_INCLUDE_LIBRARIES asio lockfree unordered program_options describe regex container)
set(BOOST_ENABLE_CMAKE ON)

FetchContent_Declare(
    boost
    GIT_REPOSITORY https://github.com/boostorg/boost.git 
    GIT_TAG boost-1.85.0
)
FetchContent_MakeAvailable(boost)

include_directories(include)

add_subdirectory(src)
add_subdirectory(app)
