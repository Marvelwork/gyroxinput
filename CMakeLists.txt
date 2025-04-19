cmake_minimum_required(VERSION 3.10)
project(ViGEmClient)
add_subdirectory(ViGEmClient)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add executable
add_executable(gyro_client gyro_client.cpp)
target_link_libraries(gyro_client PRIVATE
    debug ${CMAKE_BINARY_DIR}/ViGEmClient/Debug/ViGEmClient.lib
    optimized ${CMAKE_BINARY_DIR}/ViGEmClient/Release/ViGEmClient.lib
    setupapi
)
target_include_directories(gyro_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(gyro_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ViGEmClient/include)