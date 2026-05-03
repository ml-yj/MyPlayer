include_guard(GLOBAL)

function(myplayer_copy_qt_plugin target_name debug_file release_file output_subdir)
    if(NOT EXISTS "${debug_file}" OR NOT EXISTS "${release_file}")
        return()
    endif()

    add_custom_command(TARGET "${target_name}" POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target_name}>/${output_subdir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${debug_file},${release_file}>"
            "$<TARGET_FILE_DIR:${target_name}>/${output_subdir}/"
        VERBATIM
    )
endfunction()

if(NOT MYPLAYER_QT_ROOT OR NOT EXISTS "${MYPLAYER_QT_ROOT}/plugins")
    return()
endif()

set(MYPLAYER_QT_PLUGIN_ROOT "${MYPLAYER_QT_ROOT}/plugins")
set(MYPLAYER_QT_PLATFORM_PLUGIN_DIR "${MYPLAYER_QT_PLUGIN_ROOT}/platforms")
set(MYPLAYER_SQLITE_PLUGIN_DIR "${MYPLAYER_QT_PLUGIN_ROOT}/sqldrivers")
set(MYPLAYER_QT_MULTIMEDIA_PLUGIN_DIR "${MYPLAYER_QT_PLUGIN_ROOT}/multimedia")

myplayer_copy_qt_plugin(MyPlayer
    "${MYPLAYER_QT_PLATFORM_PLUGIN_DIR}/qwindowsd.dll"
    "${MYPLAYER_QT_PLATFORM_PLUGIN_DIR}/qwindows.dll"
    "platforms"
)

myplayer_copy_qt_plugin(MyPlayer
    "${MYPLAYER_SQLITE_PLUGIN_DIR}/qsqlited.dll"
    "${MYPLAYER_SQLITE_PLUGIN_DIR}/qsqlite.dll"
    "sqldrivers"
)

myplayer_copy_qt_plugin(MyPlayer
    "${MYPLAYER_QT_MULTIMEDIA_PLUGIN_DIR}/ffmpegmediaplugind.dll"
    "${MYPLAYER_QT_MULTIMEDIA_PLUGIN_DIR}/ffmpegmediaplugin.dll"
    "multimedia"
)

myplayer_copy_qt_plugin(MyPlayer
    "${MYPLAYER_QT_MULTIMEDIA_PLUGIN_DIR}/windowsmediaplugind.dll"
    "${MYPLAYER_QT_MULTIMEDIA_PLUGIN_DIR}/windowsmediaplugin.dll"
    "multimedia"
)
