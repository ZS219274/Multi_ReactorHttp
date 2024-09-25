cmake_minimum_required(VERSION 3.15)
project(common)

aux_source_directory(./ SRC)

add_library(common ${SRC})