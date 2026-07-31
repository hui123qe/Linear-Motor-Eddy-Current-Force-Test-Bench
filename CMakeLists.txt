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

set(DATABASE_CONFIGURATION_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/config/database.json")
if(NOT EXISTS "${DATABASE_CONFIGURATION_FILE}")
    message(FATAL_ERROR
        "Database configuration not found. Copy config/database.example.json "
        "to config/database.json and provide the local credentials.")
endif()

find_package(Qt6 6.5.3 REQUIRED COMPONENTS Widgets PrintSupport Sql)

if(NOT TARGET Qt6::QPSQLDriverPlugin)
    message(FATAL_ERROR "Qt QPSQL driver plugin target is unavailable.")
endif()

add_subdirectory(third_party/qcustomplot)
add_subdirectory(third_party/qxlsx/QXlsx EXCLUDE_FROM_ALL)

set(ACSCL_PACKAGE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/acs")
set(POSTGRESQL_RUNTIME_DIR "D:/Tool/postgresql/bin" CACHE PATH
    "Directory containing the PostgreSQL x64 client runtime DLLs")
set(POSTGRESQL_RUNTIME_DLLS
    libpq.dll
    libssl-3-x64.dll
    libcrypto-3-x64.dll
    libintl-9.dll
    libiconv-2.dll
    libwinpthread-1.dll
)
foreach(POSTGRESQL_RUNTIME_DLL IN LISTS POSTGRESQL_RUNTIME_DLLS)
    if(NOT EXISTS "${POSTGRESQL_RUNTIME_DIR}/${POSTGRESQL_RUNTIME_DLL}")
        message(FATAL_ERROR
            "PostgreSQL runtime DLL not found: ${POSTGRESQL_RUNTIME_DIR}/${POSTGRESQL_RUNTIME_DLL}")
    endif()
endforeach()

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
    src/experimentlog/ExperimentLogTypes.h
    src/experimentlog/ExperimentLogTypes.cpp
    src/experimentlog/ExperimentResultCalculator.h
    src/experimentlog/ExperimentResultCalculator.cpp
    src/experimentlog/ExperimentLogService.h
    src/experimentlog/ExperimentLogService.cpp
    src/experimentlog/ExperimentExportService.h
    src/experimentlog/ExperimentExportService.cpp
    src/acquisition/AcquisitionTypes.h
    src/acquisition/DataAcquisitionService.h
    src/acquisition/DataAcquisitionService.cpp
    src/database/AcquisitionDatabaseService.h
    src/database/AcquisitionDatabaseService.cpp
    src/motion/AcsClient.h
    src/motion/AcsClient.cpp
    src/motion/MotionControlService.h
    src/motion/MotionControlService.cpp
    src/workflow/TestExecutionService.h
    src/workflow/TestExecutionService.cpp
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
    Qt6::Sql
    QCustomPlot::QCustomPlot
    QXlsx::QXlsx
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
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:EddyCurrentBench>/sqldrivers"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::QPSQLDriverPlugin>"
        "$<TARGET_FILE_DIR:EddyCurrentBench>/sqldrivers/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/motion.json"
        "$<TARGET_FILE_DIR:EddyCurrentBench>/motion.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DATABASE_CONFIGURATION_FILE}"
        "$<TARGET_FILE_DIR:EddyCurrentBench>/database.json"
    VERBATIM
)

foreach(POSTGRESQL_RUNTIME_DLL IN LISTS POSTGRESQL_RUNTIME_DLLS)
    add_custom_command(TARGET EddyCurrentBench POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${POSTGRESQL_RUNTIME_DIR}/${POSTGRESQL_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:EddyCurrentBench>/"
        VERBATIM
    )
endforeach()

install(TARGETS EddyCurrentBench
    RUNTIME DESTINATION bin
)

install(FILES
    config/motion.json
    "${DATABASE_CONFIGURATION_FILE}"
    DESTINATION bin
)

install(FILES "$<TARGET_FILE:Qt6::QPSQLDriverPlugin>"
    DESTINATION bin/sqldrivers
)

foreach(POSTGRESQL_RUNTIME_DLL IN LISTS POSTGRESQL_RUNTIME_DLLS)
    install(FILES "${POSTGRESQL_RUNTIME_DIR}/${POSTGRESQL_RUNTIME_DLL}"
        DESTINATION bin
    )
endforeach()

install(FILES
    "${POSTGRESQL_RUNTIME_DIR}/../server_license.txt"
    "${POSTGRESQL_RUNTIME_DIR}/../commandlinetools_3rd_party_licenses.txt"
    DESTINATION licenses/postgresql
)
