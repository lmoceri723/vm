cmake_minimum_required(VERSION 3.25)
project(vm_two C)

set(CMAKE_C_STANDARD 23)

add_executable(vm_two vm.c macros.h structs.h initialize.c userapp.c system.h userapp.h)
