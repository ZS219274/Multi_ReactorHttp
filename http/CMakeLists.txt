cmake_minimum_required(VERSION 3.15)
project(http)

aux_source_directory(./ SRC)

add_library(http ${SRC})