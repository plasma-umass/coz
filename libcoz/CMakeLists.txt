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

# Add macOS-specific files on Apple platforms
if(APPLE)
    list(APPEND sources
        perf_macos.cpp
        perf_macos.h
        macho_support.cpp
        macho_support.h
        elf_compat.h
        mac_interpose.cpp
        mac_interpose.h
    )
endif()

if(APPLE)
    # On macOS, use SHARED to create a dylib that can be injected with DYLD_INSERT_LIBRARIES
    add_library(coz SHARED ${sources} ${public_headers})
    set_target_properties(coz PROPERTIES SUFFIX ".so")
else()
    # On Linux, use MODULE for LD_PRELOAD
    add_library(coz MODULE ${sources} ${public_headers})
endif()
if(CONAN_PACKAGE_VERSION)
    set_target_properties(coz PROPERTIES VERSION ${CONAN_PACKAGE_VERSION})
endif()
target_include_directories(coz
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
# Platform-specific libraries
if(APPLE)
    target_link_libraries(coz PRIVATE ${CMAKE_DL_LIBS} Threads::Threads libelfin::libelfin)
else()
    target_link_libraries(coz PRIVATE ${CMAKE_DL_LIBS} rt Threads::Threads libelfin::libelfin)
endif()

add_library(coz-instrumentation INTERFACE)
target_include_directories(coz-instrumentation
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

# Platform-specific linker flags
if(APPLE)
    target_link_libraries(coz-instrumentation INTERFACE ${CMAKE_DL_LIBS})
else()
    target_link_libraries(coz-instrumentation INTERFACE -Wl,--push-state,--no-as-needed ${CMAKE_DL_LIBS} -Wl,--pop-state)
endif()

if(INSTALL_COZ)
    install(TARGETS coz
        EXPORT coz-profilerTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/. DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
