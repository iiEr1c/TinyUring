set(HEADER_LIST
    "acceptor.hpp"
    "io_scheduler.hpp"
    "task.hpp"
    "tcp_server.hpp")
set(SRC_LIST
    "acceptor.cpp")

add_library(TinyTcpServer ${HEADER_LIST} ${SRC_LIST})

# We need this directory, and users of our library will need it too
target_include_directories(TinyTcpServer PUBLIC ./)

# All users of this library will need at least C++11
target_compile_features(TinyTcpServer PUBLIC cxx_std_20)

target_compile_options(TinyTcpServer PRIVATE
            ${CXX_FLAGS}
            "$<$<CONFIG:Debug>:${CXX_FLAGS_DEBUG}>"
            "$<$<CONFIG:Release>:${CXX_FLAGS_RELEASE}>")
#target_compile_options(TinyNet PRIVATE "-fsanitize=address" "-fsanitize=undefined")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("CMAKE_BUILD_TYPE == " ${CMAKE_BUILD_TYPE})
    target_link_options(TinyTcpServer PRIVATE ${SANITIZE_FLAG})
endif()

target_link_libraries(TinyTcpServer uring)