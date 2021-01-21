set(sources
    inspect.cpp
    libcoz.cpp
    perf.cpp
    profiler.cpp
    real.cpp)
set(headers
    ${PROJECT_SOURCE_DIR}/include/coz.h)

add_library(coz MODULE ${sources})
set_target_properties(coz PROPERTIES PUBLIC_HEADER "${headers}")
if(CONAN_PACKAGE_VERSION)
    set_target_properties(coz PROPERTIES VERSION ${CONAN_PACKAGE_VERSION})
endif()
target_include_directories(coz
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/coz>)
target_link_libraries(coz PUBLIC dl rt Threads::Threads libelfin::libelfin)

install(TARGETS coz
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/coz)

add_library(coz-instrumentation INTERFACE)
target_include_directories(coz-instrumentation
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/coz>)
target_link_libraries(coz-instrumentation INTERFACE -Wl,--push-state,--no-as-needed -ldl -Wl,--pop-state)
