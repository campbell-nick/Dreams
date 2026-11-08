cmake_minimum_required(VERSION 3.10)
project(illus VERSION 1.0.0 LANGUAGES C)

# Set C standard to C99
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Define the executable target from main.c
add_executable(illus main.c commands.c)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(illus PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wformat-security
    )
endif()

# Include GNU install directories standard (maps bin to bin, etc.)
include(GNUInstallDirs)

# Configure the install rule so it drops the binary into bin/ under the prefix
install(TARGETS illus
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME illus.version COMMAND illus --version)
    add_test(NAME illus.help COMMAND illus --help)
    add_test(NAME illus.integration
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.sh $<TARGET_FILE:illus>
    )
    set_tests_properties(illus.integration PROPERTIES TIMEOUT 180)
endif()
