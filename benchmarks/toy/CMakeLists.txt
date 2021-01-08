add_executable(toy toy.cpp)
target_link_libraries(toy PRIVATE coz-instrumentation pthread)

add_coz_run_target(run_toy COMMAND $<TARGET_FILE:toy>)
