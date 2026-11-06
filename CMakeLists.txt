cmake_minimum_required(VERSION 3.22)
project(cgui_gpui LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# 1. Vulkan
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)
message(STATUS "Found Vulkan: ${Vulkan_INCLUDE_DIRS}")

# ---------------------------------------------------------------------------
# 2. GLFW (Check host system first; fallback to FetchContent if not found)
# ---------------------------------------------------------------------------
find_package(glfw3 3.3 QUIET CONFIG)
if (NOT glfw3_FOUND)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(GLFW QUIET glfw3)
    endif()
endif()

if (TARGET glfw)
    set(GLFW_TARGET glfw)
    message(STATUS "Using system GLFW (target: glfw)")
elseif (TARGET glfw3)
    set(GLFW_TARGET glfw3)
    message(STATUS "Using system GLFW (target: glfw3)")
elseif (glfw3_FOUND)
    set(GLFW_TARGET glfw3::glfw3)
    message(STATUS "Using system GLFW (target: glfw3::glfw3)")
else()
    message(STATUS "GLFW not found on host. Fetching GLFW via FetchContent...")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glfw)
    set(GLFW_TARGET glfw)
endif()

# ---------------------------------------------------------------------------
# 3. Yoga Layout (Check host system first; fallback to FetchContent if not found)
# ---------------------------------------------------------------------------
find_package(yoga QUIET CONFIG)
if (NOT yoga_FOUND)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(YOGA QUIET yoga yogacore)
    endif()
endif()

if (TARGET yoga::yogacore)
    set(YOGA_TARGET yoga::yogacore)
    message(STATUS "Using system Yoga (target: yoga::yogacore)")
elseif (TARGET yogacore)
    set(YOGA_TARGET yogacore)
    message(STATUS "Using system Yoga (target: yogacore)")
else()
    message(STATUS "Yoga not found on host. Fetching Yoga via FetchContent...")
    set(YOGA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YOGA_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        yoga
        GIT_REPOSITORY https://github.com/facebook/yoga.git
        GIT_TAG v3.1.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(yoga)
    if (TARGET yogacore)
        set(YOGA_TARGET yogacore)
    elseif (TARGET yoga)
        set(YOGA_TARGET yoga)
    else()
        message(FATAL_ERROR "Could not find or configure Yoga target")
    endif()
endif()

# ---------------------------------------------------------------------------
# 4. GLM (Check host system first; fallback to FetchContent if not found)
# ---------------------------------------------------------------------------
find_package(glm QUIET CONFIG)
if (TARGET glm::glm)
    set(GLM_TARGET glm::glm)
    message(STATUS "Using system GLM")
else()
    message(STATUS "GLM not found on host. Fetching GLM via FetchContent...")
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glm)
    set(GLM_TARGET glm::glm)
endif()

# ---------------------------------------------------------------------------
# 5. msdfgen (Multi-channel Signed Distance Field generator)
# ---------------------------------------------------------------------------
find_package(msdfgen QUIET CONFIG)
if (TARGET msdfgen::msdfgen)
    set(MSDFGEN_TARGET msdfgen::msdfgen)
    message(STATUS "Using system msdfgen")
else()
    message(STATUS "msdfgen not found on host. Fetching msdfgen via FetchContent...")
    set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_USE_OPENMP OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_INSTALL OFF CACHE BOOL "" FORCE)
    set(MSDFGEN_USE_SKIA OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        msdfgen
        GIT_REPOSITORY https://github.com/Chlumsky/msdfgen.git
        GIT_TAG v1.12
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(msdfgen)
    if (TARGET msdfgen::msdfgen)
        set(MSDFGEN_TARGET msdfgen::msdfgen)
    elseif (TARGET msdfgen)
        set(MSDFGEN_TARGET msdfgen)
    elseif (TARGET msdfgen-core)
        set(MSDFGEN_TARGET msdfgen-core)
    endif()
endif()

# ---------------------------------------------------------------------------
# 6. Project Sources & Executable
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*.hpp
    src/*.h
)

add_executable(cgui ${SOURCES})

target_include_directories(cgui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(cgui PRIVATE
    ${GLFW_TARGET}
    ${YOGA_TARGET}
    ${GLM_TARGET}
    ${MSDFGEN_TARGET}
    Vulkan::Vulkan
)

# Automatically copy assets directory to build directory
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endif()
