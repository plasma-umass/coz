# The coz JVMTI agent, plus the coz.Coz progress-point API it binds natives to.
#
# Built only when a JDK is available. Enable with -DBUILD_JAVA_AGENT=ON.

find_package(JNI)
find_package(Java COMPONENTS Development)

if(NOT JNI_FOUND)
  message(WARNING "BUILD_JAVA_AGENT is ON but no JDK was found; skipping the Java agent")
  return()
endif()

# jvmti.h sits beside jni.h in the JDK's include directory. find_package(JNI)
# reports that directory, but only some CMake versions put it in JNI_INCLUDE_DIRS,
# so derive it from jni.h's location as well.
find_path(JVMTI_INCLUDE_DIR
  NAMES jvmti.h
  HINTS ${JAVA_INCLUDE_PATH} ${JNI_INCLUDE_DIRS}
)

if(NOT JVMTI_INCLUDE_DIR)
  message(WARNING "jvmti.h not found next to jni.h; skipping the Java agent")
  return()
endif()

# A JVMTI agent is dlopen'd by the JVM, so it is a MODULE, like libcoz itself.
add_library(cozjava MODULE agent/coz_agent.cpp)

target_include_directories(cozjava PRIVATE
  ${JNI_INCLUDE_DIRS}
  ${JVMTI_INCLUDE_DIR}
)

target_compile_features(cozjava PRIVATE cxx_std_14)

set_target_properties(cozjava PROPERTIES
  # `java -agentpath:` wants a plain path, and the JVM does not care about the
  # extension, but keep the platform-conventional one.
  PREFIX "lib"
)

if(THREADS_FOUND)
  target_link_libraries(cozjava PRIVATE Threads::Threads)
endif()

if(INSTALL_COZ)
  install(TARGETS cozjava LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/coz-profiler)
endif()

# ---------------------------------------------------------------------------
# coz.jar -- the Java-side API. Optional: the agent is useless without it, but
# a project can equally compile src/coz/Coz.java into its own build.
# ---------------------------------------------------------------------------
if(Java_JAVAC_EXECUTABLE AND Java_JAR_EXECUTABLE)
  set(COZ_JAR "${CMAKE_CURRENT_BINARY_DIR}/coz.jar")
  set(COZ_CLASSES "${CMAKE_CURRENT_BINARY_DIR}/classes")

  add_custom_command(
    OUTPUT ${COZ_JAR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${COZ_CLASSES}
    # -g keeps the line tables the agent reads back through GetLineNumberTable.
    COMMAND ${Java_JAVAC_EXECUTABLE} -g -d ${COZ_CLASSES}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/coz/Coz.java
    COMMAND ${Java_JAR_EXECUTABLE} cf ${COZ_JAR} -C ${COZ_CLASSES} coz
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/coz/Coz.java
    COMMENT "Building coz.jar"
    VERBATIM
  )

  add_custom_target(coz-jar ALL DEPENDS ${COZ_JAR})

  if(INSTALL_COZ)
    install(FILES ${COZ_JAR} DESTINATION ${CMAKE_INSTALL_DATADIR}/coz-profiler)
  endif()
else()
  message(STATUS "javac/jar not found; building the agent but not coz.jar")
endif()
