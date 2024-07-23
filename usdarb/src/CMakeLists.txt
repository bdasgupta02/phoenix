file(GLOB SOURCES *.CPP)

add_library(usdarb ${SOURES})

target_link_libraries(usdarb
    Boost::unordered
    Boost::asio
    Boost::lockfree
    Boost::log
)
