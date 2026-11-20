cmake_minimum_required(VERSION 3.21)
project(HP3488A VERSION 1.2.0 LANGUAGES CXX RC)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent Network LinguistTools)
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)
set(QT_I18N_SOURCE_LANGUAGE de)

qt_add_executable(HP3488A WIN32
    main.cpp mainwindow.cpp mainwindow.h updates.cpp carddiagram.cpp carddiagram.h hp3488.cpp hp3488.h gpib.cpp gpib.h
    resources.qrc app.rc
)
target_compile_features(HP3488A PRIVATE cxx_std_17)
target_compile_definitions(HP3488A PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN HP3488_VERSION="${PROJECT_VERSION}")
target_include_directories(HP3488A PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(HP3488A PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network)
if(MSVC)
    target_compile_options(HP3488A PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

qt_add_translations(HP3488A TS_FILES translations/hp3488_en.ts translations/hp3488_fr.ts)
foreach(locale IN ITEMS de fr)
    set(qt_catalog "${Qt6_DIR}/../../../translations/qtbase_${locale}.qm")
    if(EXISTS "${qt_catalog}")
        get_filename_component(qt_catalog "${qt_catalog}" ABSOLUTE)
        set_source_files_properties("${qt_catalog}" PROPERTIES QT_RESOURCE_ALIAS "qtbase_${locale}.qm")
        qt_add_resources(HP3488A "qt_i18n_${locale}" PREFIX "/qt-i18n" FILES "${qt_catalog}")
    endif()
endforeach()
