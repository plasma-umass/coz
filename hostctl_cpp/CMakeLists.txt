add_executable(hostctl_cpp
    main.cpp
    perf.cpp
)

set_target_properties(hostctl_cpp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)

target_link_libraries(hostctl_cpp PRIVATE pthread)
