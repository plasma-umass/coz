add_executable(sqlite-bench-modified main.c sqlite3.c)
target_compile_definitions(sqlite-bench-modified PRIVATE SQLITE_THREADSAFE=2)
target_link_libraries(sqlite-bench-modified PRIVATE sqlite3 pthread dl)

add_coz_run_target(run_sqlite_bench_modified COMMAND $<TARGET_FILE:sqlite-bench-modified>)
