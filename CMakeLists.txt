cmake_minimum_required(VERSION 3.21)

project(EddyCurrentBench
    VERSION 1.0.0
    DESCRIPTION "高速涡流测试台上位机"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5.3 REQUIRED COMPONENTS Widgets PrintSupport)

add_subdirectory(third_party/qcustomplot)

set(ACSCL_PACKAGE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/acs")
if(NOT MSVC)
    message(FATAL_ERROR "The packaged ACSCL SDK only provides MSVC-compatible artifacts.")
endif()
if(NOT EXISTS "${ACSCL_PACKAGE_ROOT}/include/ACSC.h")
    message(FATAL_ERROR "ACSCL header not found: ${ACSCL_PACKAGE_ROOT}/include/ACSC.h")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ACSCL_IMPORT_LIBRARY
        "${ACSCL_PACKAGE_ROOT}/lib/msvc/x64/ACSCL_x64.lib")
else()
    set(ACSCL_IMPORT_LIBRARY
        "${ACSCL_PACKAGE_ROOT}/lib/msvc/x86/ACSCL_x86.lib")
endif()
if(NOT EXISTS "${ACSCL_IMPORT_LIBRARY}")
    message(FATAL_ERROR "ACSCL import library not found: ${ACSCL_IMPORT_LIBRARY}")
endif()

add_library(ThirdParty::acscl UNKNOWN IMPORTED)
set_target_properties(ThirdParty::acscl PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${ACSCL_PACKAGE_ROOT}/include"
    IMPORTED_LOCATION "${ACSCL_IMPORT_LIBRARY}"
)

qt_add_executable(EddyCurrentBench
    WIN32
    src/main.cpp
    src/logging/AppLogger.h
    src/logging/AppLogger.cpp
    src/config/TestParameters.h
    src/config/TestParameters.cpp
    src/config/TestConfigurationService.h
    src/config/TestConfigurationService.cpp
    src/result/TestResultService.h
    src/result/TestResultService.cpp
    src/motion/AcsClient.h
    src/motion/AcsClient.cpp
    src/motion/MotionControlService.h
    src/motion/MotionControlService.cpp
    src/view/MainWindow.h
    src/view/MainWindow.cpp
    src/view/HeaderBar.h
    src/view/HeaderBar.cpp
    src/view/Sidebar.h
    src/view/Sidebar.cpp
    src/view/NavigationBar.h
    src/view/NavigationBar.cpp
    src/view/DeviceStatusPanel.h
    src/view/DeviceStatusPanel.cpp
    src/view/BottomStatusBar.h
    src/view/BottomStatusBar.cpp
    src/view/pages/WorkbenchPage.h
    src/view/pages/WorkbenchPage.cpp
    src/view/pages/ParametersPage.h
    src/view/pages/ParametersPage.cpp
    src/view/pages/MaintenancePage.h
    src/view/pages/MaintenancePage.cpp
    src/view/pages/RunRecordsPage.h
    src/view/pages/RunRecordsPage.cpp
    src/view/pages/AlarmPage.h
    src/view/pages/AlarmPage.cpp
    src/view/components/chart/ChartTypes.h
    src/view/components/chart/QExclusiveActionGroup.h
    src/view/components/chart/QExclusiveActionGroup.cpp
    src/view/components/chart/PlotToolBar.h
    src/view/components/chart/PlotToolBar.cpp
    src/view/components/chart/CurveSelector.h
    src/view/components/chart/CurveSelector.cpp
    src/view/components/chart/ChartWidget.h
    src/view/components/chart/ChartWidget.cpp
    src/view/widgets/StatusPill.h
    src/view/widgets/StatusPill.cpp
    src/view/widgets/MetricCard.h
    src/view/widgets/MetricCard.cpp
    src/view/widgets/ViewHelpers.h
    src/view/widgets/ViewHelpers.cpp
    resources/resources.qrc
)

target_link_libraries(EddyCurrentBench PRIVATE
    Qt6::Widgets
    QCustomPlot::QCustomPlot
    ThirdParty::acscl
)

if(MSVC)
    target_compile_options(EddyCurrentBench PRIVATE /W4 /utf-8 /permissive-)
endif()

set_target_properties(EddyCurrentBench PROPERTIES
    OUTPUT_NAME "高速涡流测试台"
    VS_GLOBAL_VcpkgEnabled "false"
)

add_custom_command(TARGET EddyCurrentBench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/motion.json"
        "$<TARGET_FILE_DIR:EddyCurrentBench>/motion.json"
    VERBATIM
)

install(TARGETS EddyCurrentBench
    RUNTIME DESTINATION bin
)

install(FILES config/motion.json
    DESTINATION bin
)
