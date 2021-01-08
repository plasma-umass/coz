add_executable(kmeans kmeans-pthread.c)
target_link_libraries(kmeans PRIVATE coz-instrumentation pthread)

add_coz_run_target(run_kmeans_small COMMAND $<TARGET_FILE:kmeans> -d 3 -c 100 -p 10000 -s 100)
add_coz_run_target(run_kmeans_large COMMAND $<TARGET_FILE:kmeans> -d 3 -c 100 -p 100000 -s 1000)
