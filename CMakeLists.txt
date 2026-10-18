cmake_minimum_required(VERSION 3.14)
project(PathExplorer CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Uses an installed raylib if found, otherwise downloads and builds it.
find_package(raylib 5.0 QUIET)
if(NOT raylib_FOUND)
  include(FetchContent)
  FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(raylib)
endif()

add_executable(PathExplorer src/main.cpp)
target_link_libraries(PathExplorer raylib)

# copy assets, GAMEEE, and critters next to the executable
add_custom_command(TARGET PathExplorer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:PathExplorer>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/GAMEEE $<TARGET_FILE_DIR:PathExplorer>/GAMEEE
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/critters $<TARGET_FILE_DIR:PathExplorer>/critters
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/floor1 $<TARGET_FILE_DIR:PathExplorer>/floor1
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/floor2 $<TARGET_FILE_DIR:PathExplorer>/floor2
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/main.png $<TARGET_FILE_DIR:PathExplorer>/main.png
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/store $<TARGET_FILE_DIR:PathExplorer>/store
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/logo.jpg $<TARGET_FILE_DIR:PathExplorer>/logo.jpg
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/intro.mpg $<TARGET_FILE_DIR:PathExplorer>/intro.mpg)
