add_executable(phoenix_conv_usd convergence/usd.cpp)
target_link_libraries(phoenix_conv_usd PUBLIC phoenix)

add_executable(phoenix_conv_eth convergence/eth.cpp)
target_link_libraries(phoenix_conv_eth PUBLIC phoenix)

add_executable(phoenix_tri triangular/main.cpp)
target_link_libraries(phoenix_tri PUBLIC phoenix)
