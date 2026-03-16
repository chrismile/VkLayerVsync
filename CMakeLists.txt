cmake_minimum_required(VERSION 3.12...4.0)
project(VkLayerVsync)

set(CMAKE_CXX_STANDARD 17)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W3 /EHsc /Zc:__cplusplus")
elseif(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()

option(USE_STATIC_STD_LIBRARIES "Link with standard libraries statically." OFF)
set(DATA_PATH "${CMAKE_SOURCE_DIR}/Data" CACHE PATH "Location of folder 'Data'")

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Headers/include")
    message(FATAL_ERROR "Error: Submodules are not cloned. Please call \"git submodule update --init --recursive\".")
endif()

file(GLOB_RECURSE SOURCES src/*.cpp src/*.c src/*.hpp src/*.h)
list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Utility-Libraries/src/layer/vk_layer_settings.cpp")
list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Utility-Libraries/src/layer/vk_layer_settings_helper.cpp")
list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Utility-Libraries/src/layer/layer_settings_util.cpp")
list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Vulkan-Utility-Libraries/src/layer/layer_settings_manager.cpp")

add_library(VkLayer_vsync SHARED ${SOURCES})
target_include_directories(VkLayer_vsync PRIVATE third_party/Vulkan-Headers/include)
target_include_directories(VkLayer_vsync PRIVATE third_party/Vulkan-Utility-Libraries/include)

if (${USE_STATIC_STD_LIBRARIES})
    if (MSYS OR MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(VkLayer_vsync PRIVATE -static-libgcc -static-libstdc++)
    endif()
    if (MSYS OR MINGW OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
        target_link_options(VkLayer_vsync PRIVATE -static -lpthread)
    endif()
endif()

if (UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(VkLayer_vsync PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/CMake/symbols_linux.map")
endif()

if (NOT WIN32)
    set(VK_LAYER_LIBRARY_PATH "./libVkLayer_vsync.so")
elseif(MSYS OR MINGW OR (${CMAKE_GENERATOR} STREQUAL "MinGW Makefiles") OR (${CMAKE_GENERATOR} STREQUAL "MSYS Makefiles"))
    set(VK_LAYER_LIBRARY_PATH ".\\\\libVkLayer_vsync.dll")
else()
    set(VK_LAYER_LIBRARY_PATH ".\\\\VkLayer_vsync.dll")
endif()
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/CMake/VkLayer_vsync.json.in"
        "${CMAKE_CURRENT_BINARY_DIR}/VkLayer_vsync.json"
)

# Copy to Debug/Release/... build directory.
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (${IS_MULTI_CONFIG})
    add_custom_command(TARGET VkLayer_vsync
            POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/VkLayer_vsync.json"
            "$<TARGET_FILE_DIR:VkLayer_vsync>")
endif()

install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/VkLayer_vsync.json"
        DESTINATION ${CMAKE_INSTALL_PREFIX}
)
install(
        TARGETS VkLayer_vsync
        LIBRARY DESTINATION .
)
