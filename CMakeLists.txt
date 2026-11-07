cmake_minimum_required(VERSION 3.10)
project(illus C)

# Set C standard to C99
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Define the executable target from main.c
add_executable(illus main.c commands.c)

# Include GNU install directories standard (maps bin to bin, etc.)
include(GNUInstallDirs)

# Configure the install rule so it drops the binary into bin/ under the prefix
install(TARGETS illus
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
