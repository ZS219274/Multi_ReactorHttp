cmake_minimum_required(VERSION 3.15)
project(tcp)

link_libraries(common http Reactor)
aux_source_directory(./ SRC)

add_library(tcp ${SRC})