set(sources
    ${PROJECT_SOURCE_DIR}/include/coz.h
    inspect.cpp
    inspect.h
    libcoz.cpp
    perf.cpp
    perf.h
    profiler.cpp
    profiler.h
    progress_point.h
    real.cpp
    real.h
    thread_state.h
    util.h
    )

add_library(coz MODULE ${sources} ${public_headers})
if(CONAN_PACKAGE_VERSION)
    set_target_properties(coz PROPERTIES VERSION ${CONAN_PACKAGE_VERSION})
endif()
target_include_directories(coz
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(coz PRIVATE ${CMAKE_DL_LIBS} rt Threads::Threads libelfin::libelfin)

add_library(coz-instrumentation INTERFACE)
target_include_directories(coz-instrumentation
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(coz-instrumentation INTERFACE -Wl,--push-state,--no-as-needed ${CMAKE_DL_LIBS} -Wl,--pop-state)

if(INSTALL_COZ)
    install(TARGETS coz
        EXPORT coz-profilerTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/. DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
