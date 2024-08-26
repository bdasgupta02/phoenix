add_executable(phoenix_conv_usd convergence/usd.cpp)
target_link_libraries(phoenix_conv_usd PUBLIC phoenix)

add_executable(phoenix_conv_eth convergence/eth.cpp)
target_link_libraries(phoenix_conv_eth PUBLIC phoenix)

add_executable(phoenix_tri_btc triangular/btc.cpp)
target_link_libraries(phoenix_tri_btc PUBLIC phoenix)

add_executable(phoenix_tri_eth triangular/eth.cpp)
target_link_libraries(phoenix_tri_eth PUBLIC phoenix)

add_executable(phoenix_tri_btc_eth triangular/btc_eth.cpp)
target_link_libraries(phoenix_tri_btc_eth PUBLIC phoenix)
