cmake_minimum_required(VERSION 3.21)
project(Wedpo VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Rust privacy core (wedpo_core)
#
# If RUST_CORE_DIR points at a prebuilt core (CI does this), we just import it.
# Otherwise cargo builds the cdylib as part of the CMake build.
# ---------------------------------------------------------------------------
set(RUST_CORE_DIR "" CACHE PATH "Directory containing the built wedpo core (core/target)")

if(RUST_CORE_DIR STREQUAL "")
    find_program(CARGO_EXEC cargo REQUIRED)
    set(RUST_CORE_DIR "${CMAKE_BINARY_DIR}/rust-core")
    set(RUST_CORE_OUT "${RUST_CORE_DIR}/release")
    if(WIN32)
        set(RUST_CORE_LIB "${RUST_CORE_OUT}/wedpo_core.dll.lib")
        set(RUST_CORE_DLL "${RUST_CORE_OUT}/wedpo_core.dll")
    else()
        set(RUST_CORE_LIB "${RUST_CORE_OUT}/libwedpo_core.so")
        set(RUST_CORE_DLL "${RUST_CORE_OUT}/libwedpo_core.so")
    endif()
    add_custom_target(wedpo_core_build
        COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${RUST_CORE_DIR}
                ${CARGO_EXEC} build --release --manifest-path "${CMAKE_SOURCE_DIR}/core/Cargo.toml"
        BYPRODUCTS "${RUST_CORE_LIB}" "${RUST_CORE_DLL}"
        VERBATIM
        USES_TERMINAL
    )
else()
    if(WIN32)
        set(RUST_CORE_LIB "${RUST_CORE_DIR}/release/wedpo_core.dll.lib")
        set(RUST_CORE_DLL "${RUST_CORE_DIR}/release/wedpo_core.dll")
    else()
        set(RUST_CORE_LIB "${RUST_CORE_DIR}/release/libwedpo_core.so")
        set(RUST_CORE_DLL "${RUST_CORE_DIR}/release/libwedpo_core.so")
    endif()
    add_custom_target(wedpo_core_build
        COMMAND ${CMAKE_COMMAND} -E echo "Using prebuilt Rust core: ${RUST_CORE_DLL}"
        VERBATIM
    )
endif()

if(NOT EXISTS "${RUST_CORE_DLL}")
    message(STATUS "Rust core not prebuilt; it will be produced by target wedpo_core_build (${RUST_CORE_DLL})")
endif()

add_library(wedpo_core SHARED IMPORTED GLOBAL)
if(WIN32)
    set_target_properties(wedpo_core PROPERTIES
        IMPORTED_LOCATION "${RUST_CORE_DLL}"
        IMPORTED_IMPLIB   "${RUST_CORE_LIB}"
    )
else()
    set_target_properties(wedpo_core PROPERTIES IMPORTED_LOCATION "${RUST_CORE_DLL}")
endif()
add_dependencies(wedpo_core wedpo_core_build)

add_subdirectory(app)
