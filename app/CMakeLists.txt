add_executable(phoenix_conv_usd convergence/usd.cpp)
target_link_libraries(phoenix_conv_usd PUBLIC phoenix)

add_executable(phoenix_conv_eth convergence/eth.cpp)
target_link_libraries(phoenix_conv_eth PUBLIC phoenix)

add_executable(phoenix_tri_btc triangular/btc.cpp)
target_link_libraries(phoenix_tri_btc PUBLIC phoenix)

add_executable(phoenix_tri_eth triangular/eth.cpp)
target_link_libraries(phoenix_tri_eth PUBLIC phoenix)

add_executable(phoenix_tri_cross triangular/cross.cpp)
target_link_libraries(phoenix_tri_cross PUBLIC phoenix)

add_executable(phoenix_data data/main.cpp)
target_link_libraries(phoenix_data PUBLIC phoenix)
