cmake_minimum_required(VERSION 3.20)
project(EarthSim VERSION 0.1.0 LANGUAGES C CXX)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/glfw)

add_library(earthsim_imgui STATIC
    externals/imgui/imgui.cpp
    externals/imgui/imgui_draw.cpp
    externals/imgui/imgui_tables.cpp
    externals/imgui/imgui_widgets.cpp
    externals/imgui/backends/imgui_impl_glfw.cpp)
target_include_directories(earthsim_imgui PUBLIC externals/imgui externals/imgui/backends)
target_link_libraries(earthsim_imgui PUBLIC glfw)

add_executable(EarthSim src/main.cpp)
target_compile_features(EarthSim PRIVATE cxx_std_17)
target_include_directories(EarthSim PRIVATE externals/glfw/deps)
target_link_libraries(EarthSim PRIVATE earthsim_imgui ${CMAKE_DL_LIBS})
set_target_properties(EarthSim PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:EarthSim>")
add_custom_target(earthsim_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:EarthSim>/shaders"
    COMMENT "Copying EarthSim shader files"
    VERBATIM)
add_dependencies(EarthSim earthsim_shaders)
install(TARGETS EarthSim RUNTIME DESTINATION .)
install(DIRECTORY shaders DESTINATION .)
