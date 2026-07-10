add_executable(sem_toy sem_toy.cpp)
target_link_libraries(sem_toy PRIVATE pthread coz-instrumentation)

add_coz_run_target(run_sem_toy COMMAND $<TARGET_FILE:sem_toy>)
