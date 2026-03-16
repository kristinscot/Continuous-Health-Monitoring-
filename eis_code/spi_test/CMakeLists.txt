cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(spi_test)

# Add source files
target_sources(app PRIVATE
    src/main.c
    src/ad5940.c
    src/Impedance.c
)

# Add include directories
target_include_directories(app PRIVATE
    src
)

# Define chip select - AD5940/AD5941 (not ADuCM355)
add_definitions(-DCHIPSEL_594X)

# Enable C99 standard
zephyr_compile_options(-std=gnu99)

# Enable floating point for math operations
zephyr_compile_options(-lm)
