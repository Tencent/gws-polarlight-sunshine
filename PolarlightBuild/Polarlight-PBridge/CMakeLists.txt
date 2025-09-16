# CMakeList.txt : CMake project for Polaris-Sunshine Bridge Package, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project ("polaris_bridge")

include_directories("../ThirdParty/xlang/Api")

FILE(GLOB Xlang_Api_SRC "../ThirdParty/xlang/Api/*.cpp")
FILE(GLOB Module_SRC "*.cpp")


add_library(${PROJECT_NAME} 
	SHARED
	${Xlang_Api_SRC}
	${Module_SRC}
)

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