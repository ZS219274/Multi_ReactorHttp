cmake_minimum_required(VERSION 3.15)
project(test)



include_directories(${PROJECT_SOURCE_DIR}/common)
include_directories(${PROJECT_SOURCE_DIR}/http)
include_directories(${PROJECT_SOURCE_DIR}/tcp)
include_directories(${PROJECT_SOURCE_DIR}/thread)
include_directories(${PROJECT_SOURCE_DIR}/Reactor)


add_subdirectory(common)
add_subdirectory(http)
add_subdirectory(Reactor)
add_subdirectory(tcp)
add_subdirectory(thread)

link_libraries(common http Reactor tcp thread pthread tcp)
add_executable(app main.cpp)