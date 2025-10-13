cmake_minimum_required(VERSION 3.20)

project(LSML)


option(LSML_BUILD_SHARED "Build LSML as a shared library" ON)

if(LSML_BUILD_SHARED)
    set(LIB_TYPE SHARED)
else()
    set(LIB_TYPE STATIC)
endif()


add_library(lsml ${LIB_TYPE}
c/lsml.h
c/lsml.c
)
set_property(TARGET lsml PROPERTY C_STANDARD 99)
set_property(TARGET lsml PROPERTY C_STANDARD_REQUIRED YES)

if (MSVC)
    target_compile_options(lsml PRIVATE /W4)
else()
    target_compile_options(lsml PRIVATE -Wall -Wextra)
endif()

# if lsml is shared, then add DLL export definitions to the header file
if (LSML_BUILD_SHARED)
target_compile_definitions(lsml PUBLIC LSML_SHARED_LIB)
target_compile_definitions(lsml PRIVATE LSML_BUILD)
endif()



install(TARGETS lsml
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES c/lsml.h
    DESTINATION include
)

# UTILITIES

add_executable(lsml_check
c/lsml_check.c
)
target_link_libraries(lsml_check PRIVATE lsml)

# TESTS

add_executable(test_hm
c/test_hm.c
)

add_executable(test_read
c/test_read.c
)
target_link_libraries(test_read PRIVATE lsml)

add_executable(test_mem_table
c/test_mem_table.c
)
target_link_libraries(test_mem_table PRIVATE lsml)

add_executable(test_mem_array
c/test_mem_array.c
)
target_link_libraries(test_mem_array PRIVATE lsml)

add_executable(test_io
c/lsml_io.h
c/test_io.c
)
target_link_libraries(test_io PRIVATE lsml)