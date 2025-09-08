cmake_minimum_required(VERSION 3.28...3.30)

# 既存テンプレートのブートストラップを使用
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(MasterLevelMeter
  VERSION 1.0.0
  LANGUAGES C CXX
)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# C++設定
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# OBS
find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

# Qt
if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# ソース
target_sources(${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/meter_widget.h
    src/meter_widget.cpp
    src/level_calc.h
    src/level_calc.cpp
)

# macOS: .plugin バンドルとして出力
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME MasterLevelMeter)

# 追加: macOS で .plugin バンドル名を明示し、ビルド成果物をユーザープラグインフォルダへコピー
if(APPLE)
  set_target_properties(${CMAKE_PROJECT_NAME}
    PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "plugin"
  )

# 出力先（ユーザー環境）
#  set(OBS_USER_PLUGIN_DIR "$ENV{HOME}/Library/Application Support/obs-studio/plugins")
#  set(PLUGIN_BUNDLE_DIR "${OBS_USER_PLUGIN_DIR}/$<TARGET_FILE_BASE_NAME:${CMAKE_PROJECT_NAME}>.plugin")

#  add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E make_directory "${OBS_USER_PLUGIN_DIR}"
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#      "$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}>"
#      "${PLUGIN_BUNDLE_DIR}"
#    COMMENT "Installing plugin bundle to ${PLUGIN_BUNDLE_DIR}"
#  )

endif()
