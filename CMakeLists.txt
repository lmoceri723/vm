# Configure Project Details
cmake_minimum_required(VERSION 3.26)
project(vm C)
set(CMAKE_C_STANDARD 11)

# Configure include directories
include_directories(include)

# Configure source files
file(GLOB SOURCES "src/*.c")

# Configure executable
add_executable(vm ${SOURCES}
        include/pagefile.h
        src/locks.c
        src/pte.c)

# Configure installation
install(TARGETS vm DESTINATION bin)
install(FILES "include/vm.h" DESTINATION include)