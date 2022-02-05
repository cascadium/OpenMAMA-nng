cmake_minimum_required (VERSION 3.11)

project (OpenMAMA-nng)

include(GoogleTest)
enable_testing()

if(UNIX)
    if(MAMA_ROOT)
        set(DEFAULT_INSTALL_PREFIX $MAMA_ROOT/lib)
    endif()
    set(DEFAULT_MAMA_ROOT "/opt/openmama")
    set(DEFAULT_NNG_ROOT "/home/fquinn/lib/nng")
else()
    if(CMAKE_CL_64)
        set(DEFAULT_INSTALL_PREFIX $ENV{ProgramW6432})
    else()
        set(DEFAULT_INSTALL_PREFIX $ENV{PROGRAMFILES})
    endif()

    # Note path is relative to where it would be used
    set(DEFAULT_MAMA_ROOT "${DEFAULT_INSTALL_PREFIX}/OpenMAMA")
    set(DEFAULT_EVENT_ROOT "${DEFAULT_INSTALL_PREFIX}/libevent")
endif()

if(NOT MAMA_ROOT)
    set(MAMA_ROOT ${DEFAULT_MAMA_ROOT})
endif()

if(NOT NNG_ROOT)
    set(NNG_ROOT ${DEFAULT_NNG_ROOT})
endif()

if(NOT EVENT_ROOT)
    set(EVENT_ROOT ${DEFAULT_EVENT_ROOT})
endif()

if (MSVC)
  set (OPENMAMA_LIBS libmamacmd libwombatcommonmd)
else (NOT MSVC)
  set (OPENMAMA_LIBS mama pthread wombatcommon)
endif()

add_subdirectory(src)

# We'll need OpenMAMA's source code for unit test code
include(FetchContent)
FetchContent_Declare(
  openmama
  GIT_REPOSITORY https://github.com/finos/OpenMAMA.git
  GIT_TAG        next
)
FetchContent_Populate(openmama)

# We need gtest for unit tests
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        release-1.10.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GTEST ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include_directories(${MAMA_ROOT}/include)
include_directories(${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c)
include_directories(${openmama_SOURCE_DIR}/mama/c_cpp/src/c)
link_directories(${MAMA_ROOT}/lib)
link_directories(${GTEST_ROOT}/lib)

message("MAMA_ROOT = ${MAMA_ROOT}")

add_executable(UnitTestMamaC
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/MainUnitTestC.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/inboxtest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/iotest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/mamainternaltest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/msgutils.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/openclosetest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/publishertest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/queuetest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/dictionarytest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/subscriptiontest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/timertest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/transporttest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/fieldcache/fieldcachevectortest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/fieldcache/fieldcachelisttest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/fieldcache/fieldcachemaptest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/fieldcache/fieldcachefieldtest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/fieldcache/fieldcacheiteratortest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/fieldcache/fieldcacherecordtest.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/fieldcache/fieldcachetest.cpp
)
add_dependencies(UnitTestMamaC mamanngimpl${MAMA_LIB_SUFFIX})
target_link_libraries(UnitTestMamaC ${OPENMAMA_LIBS} gtest)
gtest_discover_tests(
    UnitTestMamaC
    EXTRA_ARGS -m nng -p qpidmsg -i Q
    PROPERTIES ENVIRONMENT
        "WOMBAT_PATH=${openmama_SOURCE_DIR}/mama/c_cpp/src/examples:${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c"
)

add_executable(UnitTestMamaMiddlewareC
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/MainUnitTestC.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/middleware/middlewareGeneralTests.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/middleware/middlewareInboxTests.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/middleware/middlewareIoTests.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/middleware/middlewareMsgTests.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/middleware/middlewarePublisherTests.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/middleware/middlewareQueueTests.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/middleware/middlewareSubscriptionTests.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/middleware/middlewareTimerTests.cpp
    ${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c/middleware/middlewareTransportTests.cpp
)
add_dependencies(UnitTestMamaMiddlewareC mamanngimpl${MAMA_LIB_SUFFIX})
target_link_libraries(UnitTestMamaMiddlewareC ${OPENMAMA_LIBS} gtest)
gtest_discover_tests(
    UnitTestMamaMiddlewareC
    EXTRA_ARGS -m nng -p qpidmsg -i Q
    PROPERTIES ENVIRONMENT
        "WOMBAT_PATH=${openmama_SOURCE_DIR}/mama/c_cpp/src/examples:${openmama_SOURCE_DIR}/mama/c_cpp/src/gunittest/c"
)
