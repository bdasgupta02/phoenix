cmake_minimum_required(VERSION 3.20)
project(phoenix CXX)

find_package(OpenSSL REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mtune=native -pthread -O3 -flto=auto -finline-functions")

include(FetchContent)

set(BOOST_INCLUDE_LIBRARIES log asio lockfree unordered program_options describe regex container)
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
