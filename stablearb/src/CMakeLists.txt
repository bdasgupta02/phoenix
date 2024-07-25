file(GLOB SOURCES *.cpp)

add_library(stablearb ${SOURCES})

target_link_libraries(stablearb PUBLIC
    Boost::program_options
    Boost::unordered
    Boost::lockfree
    Boost::describe
    Boost::asio
    Boost::log
)

target_include_directories(stablearb PUBLIC ${PROJECT_SOURCE_DIR}/include)
