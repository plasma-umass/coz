add_executable(lock_test lock_test.cpp)
target_link_libraries(lock_test PRIVATE pthread coz-instrumentation)

add_coz_run_target(run_lock_test COMMAND $<TARGET_FILE:lock_test>)
