if(NOT USE_CODEC_VORBIS AND NOT USE_CODEC_OPUS)
    return()
endif()



include(utils/disable_warnings)

set(INTERNAL_OGG_DIR ${SOURCE_DIR}/thirdparty/libogg-1.3.6)

if(USE_INTERNAL_OGG)
    file(GLOB_RECURSE OGG_SOURCES ${INTERNAL_OGG_DIR}/*.c)
    disable_warnings(${OGG_SOURCES})
    set(OGG_INCLUDE_DIRS ${INTERNAL_OGG_DIR}/include)
    
    if(BUILD_CLIENT)
        list(APPEND CLIENT_LIBRARY_SOURCES ${OGG_SOURCES})
    endif()
    
    if(BUILD_SERVER)
        list(APPEND SERVER_LIBRARY_SOURCES ${OGG_SOURCES})
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OGG REQUIRED ogg)
endif()

if(BUILD_CLIENT)
    list(APPEND CLIENT_LIBRARIES ${OGG_LIBRARIES})
    list(APPEND CLIENT_INCLUDE_DIRS ${OGG_INCLUDE_DIRS})
    list(APPEND CLIENT_DEFINITIONS ${OGG_DEFINITIONS})
endif()

if(BUILD_SERVER)
    list(APPEND SERVER_LIBRARIES ${OGG_LIBRARIES})
    list(APPEND SERVER_INCLUDE_DIRS ${OGG_INCLUDE_DIRS})
    list(APPEND SERVER_DEFINITIONS ${OGG_DEFINITIONS})
endif()
