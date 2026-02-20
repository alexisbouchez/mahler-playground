cmake_minimum_required(VERSION 3.10)
project(musical_horoscope C)

set(CMAKE_C_STANDARD 99)
set(MAHLER_PATH "${PROJECT_SOURCE_DIR}/mahler.c")

add_executable(horoscope main.c)
target_include_directories(
    horoscope PUBLIC
    "${MAHLER_PATH}/inc"
    "${MAHLER_PATH}/src"
)

add_subdirectory(${MAHLER_PATH})
target_link_libraries(horoscope PUBLIC mahler)
