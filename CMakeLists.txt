cmake_minimum_required(VERSION 3.21)

project(GerritPilotNext VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Network Svg)

qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(src)
