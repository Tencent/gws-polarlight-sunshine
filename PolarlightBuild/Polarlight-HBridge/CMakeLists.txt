# CMakeList.txt : CMake project for Polaris, include source and define
# project specific logic here.
#
cmake_minimum_required(VERSION 3.8)
project ("HBridge")


include_directories("../ThirdParty/xlang/Api")
include_directories("../Inc")

FILE(GLOB Xlang_Api_SRC "../ThirdParty/xlang/Api/*.cpp")
FILE(GLOB Common_SRC  "../Inc/wait.cpp")
FILE(GLOB AppEntry_SRC "*.cpp")

add_executable (${PROJECT_NAME}
    ${Xlang_Api_SRC}
    ${Common_SRC}
    ${AppEntry_SRC})

 if(MSVC)
    # Use the static C library for all build types
    foreach(var
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
      )
      if(${var} MATCHES "/MD")
        string(REGEX REPLACE "/MD" "/MT" ${var} "${${var}}")
      endif()
    endforeach()
endif(MSVC)