cmake_minimum_required(VERSION 3.26)

project(MyPlayerWorkspace LANGUAGES NONE)

add_subdirectory(src/MyPlayer)

if(TARGET MyPlayer)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT MyPlayer)
endif()
