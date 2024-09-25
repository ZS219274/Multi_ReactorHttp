cmake_minimum_required(VERSION 3.15)
project(Reactor)

aux_source_directory(./ SRC)

add_library(Reactor ${SRC})