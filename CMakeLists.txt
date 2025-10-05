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
  target_link_libraries(crypto_trading PRIVATE CURL::libcurl Threads::Threads ${CURSES_LIBRARIES} ${Boost_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto strategies_lib ZLIB::ZLIB)
else()
  target_link_libraries(crypto_trading PRIVATE CURL::libcurl Threads::Threads ${Boost_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto strategies_lib ZLIB::ZLIB)
endif()

# Install target
install(TARGETS crypto_trading DESTINATION bin)

add_executable(backtest tools/backtest.cpp)
target_link_libraries(backtest PRIVATE strategies_lib)
target_include_directories(backtest PRIVATE strategies include)

add_executable(test_sse_client tools/test_sse_client.cpp)
target_include_directories(test_sse_client PRIVATE include)

add_executable(tick2bin tools/tick2bin.cpp)
target_include_directories(tick2bin PRIVATE include)

add_executable(aggregate_1m tools/aggregate_1m.cpp)
target_include_directories(aggregate_1m PRIVATE include)

add_executable(downloader tools/downloader.cpp)
target_include_directories(downloader PRIVATE include)
target_link_libraries(downloader PRIVATE CURL::libcurl)

find_package(ZLIB REQUIRED)
target_link_libraries(aggregate_1m PRIVATE ZLIB::ZLIB)

add_executable(proxy_traffic_monitor tools/proxy_traffic_monitor.cpp)
target_link_libraries(proxy_traffic_monitor PRIVATE ${Boost_LIBRARIES})
target_include_directories(proxy_traffic_monitor PRIVATE include)

enable_testing()
add_test(NAME smoke_klines_archive COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_klines_archive.sh)
