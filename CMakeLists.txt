cmake_minimum_required(VERSION 3.20)
project(EarthSim VERSION 0.1.0 LANGUAGES C CXX)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/glfw)

find_package(OpenGL REQUIRED)
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(EARTHSIM_GLEW_ARCH x64)
    else()
        set(EARTHSIM_GLEW_ARCH Win32)
    endif()
    add_library(earthsim_glew STATIC IMPORTED)
    set_target_properties(earthsim_glew PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/externals/glew/lib/Release/${EARTHSIM_GLEW_ARCH}/glew32s.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/externals/glew/include")
    set(EARTHSIM_GLEW_TARGET earthsim_glew)
else()
    find_package(GLEW REQUIRED)
    set(EARTHSIM_GLEW_TARGET GLEW::GLEW)
endif()

add_library(earthsim_imgui STATIC
    externals/imgui/imgui.cpp
    externals/imgui/imgui_draw.cpp
    externals/imgui/imgui_tables.cpp
    externals/imgui/imgui_widgets.cpp
    externals/imgui/backends/imgui_impl_glfw.cpp
    externals/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(earthsim_imgui PUBLIC externals/imgui externals/imgui/backends)
target_link_libraries(earthsim_imgui PUBLIC glfw)

add_executable(EarthSim
    src/main.cpp
    src/Application.cpp
    src/Application.hpp
    src/Common.cpp
    src/Common.hpp
    src/Physics.cpp
    src/Physics.hpp
    src/Rendering.cpp
    src/Rendering.hpp
    src/Shaders.cpp
    src/Shaders.hpp
    src/Terrain.cpp
    src/Terrain.hpp)
target_compile_features(EarthSim PRIVATE cxx_std_17)
target_include_directories(EarthSim PRIVATE externals/glew/include)
if(WIN32)
    target_compile_definitions(EarthSim PRIVATE GLEW_STATIC)
endif()
target_link_libraries(EarthSim PRIVATE earthsim_imgui ${EARTHSIM_GLEW_TARGET} OpenGL::GL ${CMAKE_DL_LIBS})
set_target_properties(EarthSim PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:EarthSim>")
add_custom_target(earthsim_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:EarthSim>/shaders"
    COMMENT "Copying EarthSim shader files"
    VERBATIM)
add_dependencies(EarthSim earthsim_shaders)
install(TARGETS EarthSim RUNTIME DESTINATION .)
install(DIRECTORY shaders DESTINATION .)
