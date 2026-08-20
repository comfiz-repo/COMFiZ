cmake_minimum_required(VERSION 3.13)
project(COMFiZ C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(MASKORDERS 1 2 3 4 5 CACHE STRING "masking orders to build")

add_compile_options(-O3
    -ftrapv -fstack-protector-strong -Wall -Wextra -Wpedantic
    -Wconversion -Wsign-conversion -Wformat=2 -Wvla -Winit-self)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(FALSE_POSITIVES "-Wno-maybe-uninitialized;-Wno-stringop-overflow")
endif()
set_source_files_properties(COMFiZ/gadgets.c BPTBC24/gadgets.c PROPERTIES
    COMPILE_OPTIONS "${FALSE_POSITIVES}")

file(GLOB COMFIZ_SRC  CONFIGURE_DEPENDS COMFiZ/*.c)
file(GLOB BPTBC24_SRC CONFIGURE_DEPENDS BPTBC24/*.c)

function(bench name lib src)
    add_executable(${name} ${src})
    target_compile_definitions(${name} PRIVATE ${ARGN})
    target_link_libraries(${name} ${lib} m)
endfunction()

foreach(d IN LISTS MASKORDERS)
    add_library(comfiz_gadgets_d${d} SHARED ${COMFIZ_SRC})
    target_include_directories(comfiz_gadgets_d${d} PUBLIC COMFiZ)
    target_compile_definitions(comfiz_gadgets_d${d} PUBLIC MASKORDER=${d})
    target_link_libraries(comfiz_gadgets_d${d} m)

    add_library(bptbc24_gadgets_d${d} SHARED ${BPTBC24_SRC})
    target_include_directories(bptbc24_gadgets_d${d} PUBLIC BPTBC24)
    target_compile_definitions(bptbc24_gadgets_d${d} PUBLIC MASKORDER=${d})
    target_link_libraries(bptbc24_gadgets_d${d} m)

    add_library(bptbc24_nr_gadgets_d${d} SHARED ${BPTBC24_SRC})
    target_include_directories(bptbc24_nr_gadgets_d${d} PUBLIC BPTBC24)
    target_compile_definitions(bptbc24_nr_gadgets_d${d} PUBLIC MASKORDER=${d} BPTBC24_NR)
    target_link_libraries(bptbc24_nr_gadgets_d${d} m)

    # samplers
    bench(comfiz_d${d}     comfiz_gadgets_d${d}     apps/main.c SAMPLER_COMFIZ)
    bench(bptbc24_d${d}    bptbc24_gadgets_d${d}    apps/main.c SAMPLER_BPTBC24)
    bench(bptbc24_nr_d${d} bptbc24_nr_gadgets_d${d} apps/main.c SAMPLER_BPTBC24 BPTBC24_NR)

    # gadgets, in the order the scripts run them: the reciprocal, the exponential, then BerExp
    foreach(g SECFXPINV SECFXPEXP SECFXPBEREXP)
        string(TOLOWER ${g} gl)
        bench(bench_${gl}_d${d} comfiz_gadgets_d${d} apps/bench_gadgets_main.c BENCH_${g})
    endforeach()
    foreach(g SECFPRINVNR SECAPPROXEXP SECFPRBEREXP)
        string(TOLOWER ${g} gl)
        bench(bench_${gl}_d${d} bptbc24_gadgets_d${d} apps/bench_gadgets_main.c BENCH_${g})
    endforeach()
endforeach()
