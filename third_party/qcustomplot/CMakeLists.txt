file(GLOB_RECURSE QCUSTOMPLOT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE QCUSTOMPLOT_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(qcustomplot STATIC EXCLUDE_FROM_ALL
    ${QCUSTOMPLOT_SOURCES}
    ${QCUSTOMPLOT_HEADERS}
)
add_library(QCustomPlot::QCustomPlot ALIAS qcustomplot)

target_include_directories(qcustomplot
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(qcustomplot
    PUBLIC
        Qt6::Widgets
        Qt6::PrintSupport
)

target_compile_features(qcustomplot PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(qcustomplot PRIVATE /utf-8)
endif()

set_target_properties(qcustomplot PROPERTIES
    AUTOMOC ON
    DEBUG_POSTFIX d
    FOLDER "third_party"
    OUTPUT_NAME "qcustomplot"
    VERSION "2.1.1"
)
