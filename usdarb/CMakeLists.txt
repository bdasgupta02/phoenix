cmake_minimum_required(VERSION 3.15)
project(usdarb CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -pthread -03")

include(FetchContent)

set(BOOST_INCLUDE_LIBRARIES log asio lockfree unordered)
set(BOOST_ENABLE_CMAKE ON)

FetchContent_Declare(
    boost
    GIT_REPOSITORY https://github.com/boostorg/boost.git 
    GIT_TAG boost-1.85.0
)
FetchContent_MakeAvailable(boost)

include_directories(include)

add_subdirectory(src)
