include_directories(.)
include_directories(${MAMA_ROOT}/include)
include_directories(${EVENT_ROOT}/include)
include_directories(${ZMQ_ROOT}/include)
include_directories(${DEFAULT_NNG_ROOT}/include)

link_directories(${MAMA_ROOT}/lib)
link_directories(${MAMA_ROOT}/lib/dynamic)
link_directories(${MAMA_ROOT}/lib/dynamic-debug)
link_directories(${ZMQ_ROOT}/lib)
link_directories(${EVENT_ROOT}/lib)
link_directories(${DEFAULT_NNG_ROOT}/lib)

add_definitions(-DBRIDGE -DMAMA_DLL -DOPENMAMA_INTEGRATION)

if(WIN32)
    if (CMAKE_BUILD_TYPE MATCHES "Debug")
        set(MAMA_LIB_SUFFIX "mdd")
    else()
        set(MAMA_LIB_SUFFIX "md")
    endif()
else()
    set(MAMA_LIB_SUFFIX "")
endif()

add_library(mamanngimpl${MAMA_LIB_SUFFIX}
            MODULE bridge.c
                   msg.c
                   msg.h
                   publisher.c
                   subscription.c
                   subscription.h
                   transport.c
                   transport.h
        nngbridgefunctions.h
        nng.h)

if(WIN32)
    target_link_libraries(mamanngimpl${MAMA_LIB_SUFFIX}
                          libwombatcommon${MAMA_LIB_SUFFIX}
                          libmamac${MAMA_LIB_SUFFIX}
                          event
                          uuid
                          Ws2_32)

    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set_target_properties(mamanngimpl${MAMA_LIB_SUFFIX} PROPERTIES PREFIX "lib")

    # Windows Targets
    install(TARGETS mamanngimpl${MAMA_LIB_SUFFIX}
            CONFIGURATIONS Release
            DESTINATION bin/dynamic)
    install(TARGETS mamanngimpl${MAMA_LIB_SUFFIX}
            CONFIGURATIONS Debug
            DESTINATION bin/dynamic-debug)
elseif(UNIX)
    target_link_libraries(mamanngimpl${MAMA_LIB_SUFFIX}
                          mamabaseimpl${MAMA_LIB_SUFFIX}
                          wombatcommon
                          mama
                          nng
                          uuid
                          event)
    install(TARGETS mamanngimpl${MAMA_LIB_SUFFIX} DESTINATION lib)
endif()

# Default to installing directly to MAMA directory unless provided already
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set (CMAKE_INSTALL_PREFIX "${MAMA_ROOT}" CACHE PATH "default install path" FORCE)
endif()
