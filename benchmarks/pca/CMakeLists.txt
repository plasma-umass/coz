add_executable(pca pca-pthread.c)
target_link_libraries(pca PRIVATE coz-instrumentation pthread)

add_coz_run_target(run_pca COMMAND $<TARGET_FILE:pca> > /dev/null)
