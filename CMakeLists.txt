cmake_minimum_required(VERSION 3.10)
project(mahler_playground C)

set(CMAKE_C_STANDARD 99)
set(MAHLER_PATH "${PROJECT_SOURCE_DIR}/mahler.c")

add_subdirectory(${MAHLER_PATH})

# Musical Horoscope
add_executable(horoscope main.c)
target_include_directories(horoscope PUBLIC "${MAHLER_PATH}/inc" "${MAHLER_PATH}/src")
target_link_libraries(horoscope PUBLIC mahler)

# Cursed Composer (WAV generator)
add_executable(composer composer.c)
target_include_directories(composer PUBLIC "${MAHLER_PATH}/inc" "${MAHLER_PATH}/src")
target_link_libraries(composer PUBLIC mahler m)
