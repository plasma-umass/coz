add_executable(sqlite-bench main.c sqlite3.c)
target_compile_definitions(sqlite-bench PRIVATE SQLITE_THREADSAFE=2)
target_link_libraries(sqlite-bench PRIVATE sqlite3 pthread dl)

add_coz_run_target(run_sqlite_bench COMMAND $<TARGET_FILE:sqlite-bench>)
