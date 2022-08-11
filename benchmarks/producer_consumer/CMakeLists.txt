add_executable(producer_consumer producer_consumer.cpp)
target_link_libraries(producer_consumer PRIVATE pthread)

add_coz_run_target(run_producer_consumer COMMAND $<TARGET_FILE:producer_consumer>)
