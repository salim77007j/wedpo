find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Sql WebEngineCore WebEngineWidgets PrintSupport Svg WebChannel)

qt_standard_project_setup()

set(APP_SOURCES
    src/main.cpp
    src/settings.cpp
    src/privacy.cpp
    src/interceptor.cpp
    src/stores.cpp
    src/downloads.cpp
    src/webpage.cpp
    src/webview.cpp
    src/omnibox.cpp
    src/tabstrip.cpp
    src/toolbar.cpp
    src/findbar.cpp
    src/theme.cpp
    src/bridge.cpp
    src/pages.cpp
    src/mainwindow.cpp
)

set(APP_HEADERS
    src/settings.h
    src/privacy.h
    src/interceptor.h
    src/stores.h
    src/downloads.h
    src/webpage.h
    src/webview.h
    src/omnibox.h
    src/tabstrip.h
    src/toolbar.h
    src/findbar.h
    src/theme.h
    src/bridge.h
    src/pages.h
    src/mainwindow.h
)

list(APPEND APP_SOURCES assets/assets.qrc)

add_executable(wedpo-browser WIN32 ${APP_SOURCES} ${APP_HEADERS})
target_include_directories(wedpo-browser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wedpo-browser PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Sql
    Qt6::WebEngineCore Qt6::WebEngineWidgets Qt6::PrintSupport Qt6::Svg Qt6::WebChannel
    wedpo_core
)

# ---------------------------------------------------------------- deploy ----
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_HOST_PATH}/bin" "${QT_BIN_DIR}")
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/dist"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:wedpo-browser>" "${CMAKE_BINARY_DIR}/dist/"
        COMMAND ${CMAKE_COMMAND} -E copy "${RUST_CORE_DLL}" "${CMAKE_BINARY_DIR}/dist/"
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release --no-translations --compiler-runtime --system-d3d-compiler --opengl software "${CMAKE_BINARY_DIR}/dist/wedpo-browser.exe"
        DEPENDS wedpo-browser
        COMMENT "Deploying Windows bundle"
        VERBATIM
    )
else()
    set_target_properties(wedpo-browser PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
        BUILD_RPATH   "${RUST_CORE_DIR}/release"
    )
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
        COMMAND bash "${CMAKE_SOURCE_DIR}/scripts/package-linux.sh"
                "$<TARGET_FILE:wedpo-browser>" "${CMAKE_BINARY_DIR}/dist" "${QT_HOST_PATH}"
        DEPENDS wedpo-browser
        COMMENT "Deploying Linux bundle"
        VERBATIM
        USES_TERMINAL
    )
endif()
