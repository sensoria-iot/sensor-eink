cmake_minimum_required(VERSION 3.16.0)

# Add RainMaker components and other common application components
set(EXTRA_COMPONENT_DIRS common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(epdiy_as_component)
