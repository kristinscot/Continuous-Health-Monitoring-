cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(advertise_hardware_timed_ADC_read)

target_sources(app PRIVATE src/main.c)
