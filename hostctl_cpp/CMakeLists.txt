cmake_minimum_required(VERSION 3.13)
project(hostctl_cpp)

# 1) C++17 활성화 ― Add_executable 전에 두는 편이 관례지만 기능상 차이는 없음
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

# 2) 소스 목록
add_executable(hostctl_cpp
    main.cpp
    perf.cpp
)

# 3) pthread 찾기
find_package(Threads REQUIRED)

# 4) 필요한 라이브러리 연결
target_link_libraries(hostctl_cpp
    PRIVATE
        Threads::Threads   # -lpthread
        rt                 # -lrt (timer_create·timer_settime)
        CURL::libcurl                       # ★ 추가
)

# 5) GCC 7 / 8 전용 <filesystem> 추가 아카이브
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
    CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(hostctl_cpp PRIVATE stdc++fs)
endif()

