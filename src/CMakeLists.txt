add_library(phoenix 
  data/fix.cpp
  strategies/triangular/utils.cpp
)

target_link_libraries(phoenix PUBLIC
    Boost::program_options
    Boost::unordered
    Boost::container
    Boost::lockfree
    Boost::describe
    Boost::asio
    Boost::regex
    OpenSSL::Crypto
)

target_include_directories(phoenix PUBLIC ${PROJECT_SOURCE_DIR}/include)
