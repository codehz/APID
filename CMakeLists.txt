cmake_minimum_required(VERSION 3.13)

project(apid)
enable_language(C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(APID_USE_LIBUV "Use libuv for apid (only for node addon)" OFF)

include_directories(include)

if (NOT ${APID_USE_LIBUV})
  add_library(ae ae/ae.c ae/zmalloc.c)
  target_include_directories(ae INTERFACE ae)
endif (NOT ${APID_USE_LIBUV})

add_library(hiredis hiredis/hiredis.c hiredis/async.c hiredis/sds.c hiredis/net.c hiredis/read.c)
target_include_directories(hiredis INTERFACE hiredis)

add_library(apid src/apid.c)
target_include_directories(apid INTERFACE include)
target_link_libraries(apid PRIVATE hiredis)
if (${APID_USE_LIBUV})
  target_compile_definitions(apid PUBLIC APID_USE_LIBUV=1)
else (${APID_USE_LIBUV})
  target_link_libraries(apid PRIVATE ae)
endif (${APID_USE_LIBUV})

if (NOT ${APID_USE_LIBUV})
  add_executable(apid-test tests/test.c)
  target_link_libraries(apid-test apid)

  add_executable(apid-test-client tests/test-client.c)
  target_link_libraries(apid-test-client apid)
endif (NOT ${APID_USE_LIBUV})