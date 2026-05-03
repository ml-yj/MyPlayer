include_guard(GLOBAL)

set(MYPLAYER_QT_ROOT "" CACHE PATH "Qt 6 installation root")

function(myplayer_resolve_qt_root)
    if(EXISTS "${MYPLAYER_QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake")
        return()
    endif()

    unset(_myplayer_qt6_dir_hint)

    if(DEFINED Qt6_DIR AND Qt6_DIR AND EXISTS "${Qt6_DIR}/Qt6Config.cmake")
        set(_myplayer_qt6_dir_hint "${Qt6_DIR}")
    elseif(DEFINED ENV{Qt6_DIR} AND NOT "$ENV{Qt6_DIR}" STREQUAL "" AND EXISTS "$ENV{Qt6_DIR}/Qt6Config.cmake")
        set(_myplayer_qt6_dir_hint "$ENV{Qt6_DIR}")
    endif()

    if(_myplayer_qt6_dir_hint)
        get_filename_component(_myplayer_qt_cmake_dir "${_myplayer_qt6_dir_hint}" DIRECTORY)
        get_filename_component(_myplayer_qt_lib_dir "${_myplayer_qt_cmake_dir}" DIRECTORY)
        get_filename_component(_myplayer_qt_root_from_qt6_dir "${_myplayer_qt_lib_dir}" DIRECTORY)
        if(EXISTS "${_myplayer_qt_root_from_qt6_dir}/lib/cmake/Qt6/Qt6Config.cmake")
            set(MYPLAYER_QT_ROOT "${_myplayer_qt_root_from_qt6_dir}" CACHE PATH "Qt 6 installation root" FORCE)
            return()
        endif()
    endif()

    foreach(_myplayer_qt_root_hint IN ITEMS "$ENV{MYPLAYER_QT_ROOT}" "$ENV{QTDIR}" "$ENV{Qt6_ROOT}")
        if(_myplayer_qt_root_hint AND EXISTS "${_myplayer_qt_root_hint}/lib/cmake/Qt6/Qt6Config.cmake")
            set(MYPLAYER_QT_ROOT "${_myplayer_qt_root_hint}" CACHE PATH "Qt 6 installation root" FORCE)
            return()
        endif()
    endforeach()

    foreach(_myplayer_qt_search_root IN ITEMS "D:/Qt" "C:/Qt" "$ENV{USERPROFILE}/Qt")
        if(NOT _myplayer_qt_search_root OR NOT EXISTS "${_myplayer_qt_search_root}")
            continue()
        endif()

        if(EXISTS "${_myplayer_qt_search_root}/lib/cmake/Qt6/Qt6Config.cmake")
            set(MYPLAYER_QT_ROOT "${_myplayer_qt_search_root}" CACHE PATH "Qt 6 installation root" FORCE)
            return()
        endif()

        file(GLOB _myplayer_qt_version_dirs LIST_DIRECTORIES true "${_myplayer_qt_search_root}/*")
        list(SORT _myplayer_qt_version_dirs COMPARE NATURAL ORDER DESCENDING)

        foreach(_myplayer_qt_version_dir IN LISTS _myplayer_qt_version_dirs)
            foreach(_myplayer_qt_kit IN ITEMS msvc2022_64 msvc2019_64 clang_64 mingw_64)
                if(EXISTS "${_myplayer_qt_version_dir}/${_myplayer_qt_kit}/lib/cmake/Qt6/Qt6Config.cmake")
                    set(MYPLAYER_QT_ROOT "${_myplayer_qt_version_dir}/${_myplayer_qt_kit}" CACHE PATH "Qt 6 installation root" FORCE)
                    return()
                endif()
            endforeach()
        endforeach()
    endforeach()
endfunction()

myplayer_resolve_qt_root()

if(EXISTS "${MYPLAYER_QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake")
    set(Qt6_DIR "${MYPLAYER_QT_ROOT}/lib/cmake/Qt6" CACHE PATH "Qt 6 package configuration directory" FORCE)
    list(PREPEND CMAKE_PREFIX_PATH "${MYPLAYER_QT_ROOT}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Multimedia MultimediaWidgets Sql)
