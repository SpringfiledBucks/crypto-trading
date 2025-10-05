cmake_minimum_required(VERSION 3.10)
project(crypto_trading_cpp VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
find_package(Curses REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

add_executable(crypto_trading
  src/main.cpp
  src/http_client.cpp
  src/data_publisher.cpp
  src/indicators.cpp
  src/webconsole_server.cpp
  src/trader.cpp
  src/console_ui.cpp
  src/websocket_client.cpp
  src/order_manager.cpp
  src/binance_api.cpp
)

add_library(strategies_lib STATIC
  strategies/elliott_harmonic_fib.cpp
)

target_include_directories(strategies_lib PRIVATE strategies include)


target_include_directories(crypto_trading PRIVATE include)

if(CURSES_FOUND)
  target_include_directories(crypto_trading PRIVATE ${CURSES_INCLUDE_DIRS})
  target_link_libraries(crypto_trading PRIVATE CURL::libcurl Threads::Threads ${CURSES_LIBRARIES} ${Boost_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto strategies_lib)
else()
  target_link_libraries(crypto_trading PRIVATE CURL::libcurl Threads::Threads ${Boost_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto strategies_lib)
endif()

# Install target
install(TARGETS crypto_trading DESTINATION bin)

add_executable(backtest tools/backtest.cpp)
target_link_libraries(backtest PRIVATE strategies_lib)
target_include_directories(backtest PRIVATE strategies include)

add_executable(test_sse_client tools/test_sse_client.cpp)
target_include_directories(test_sse_client PRIVATE include)
