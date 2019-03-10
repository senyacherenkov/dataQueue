cmake_minimum_required(VERSION 3.5)

project(dataQueue)
#VERSION 0.0.$ENV{TRAVIS_BUILD_NUMBER})

find_package(Boost COMPONENTS unit_test_framework REQUIRED)

set(CMAKE_BUILD_TYPE DEBUG)
include_directories(${Boost_INCLUDE_DIR})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(${PROJECT_NAME} SHARED "dataqueue.h")
add_executable(${PROJECT_NAME}_test "main.cpp")

set_target_properties(${PROJECT_NAME} ${PROJECT_NAME}_test PROPERTIES
  LINKER_LANGUAGE CXX
  CXX_STANDARD 14
  CXX_STANDARD_REQUIRED ON
  COMPILE_OPTIONS -Wpedantic -Wall -Wextra
)

add_compile_options(lboost_system)

target_link_libraries(${PROJECT_NAME}
        Threads::Threads
)

target_link_libraries(${PROJECT_NAME}_test
        Threads::Threads
        ${Boost_LIBRARIES}
        ${PROJECT_NAME}
)

set_target_properties(${PROJECT_NAME}_test PROPERTIES
    COMPILE_DEFINITIONS BOOST_TEST_DYN_LINK
    INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR}
)


set(CPACK_GENERATOR DEB)

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")

set(CPACK_PACKAGE_CONTACT senyacherenkov@yandex.ru)

include (CPack)

enable_testing()

add_test(test_version_valid ${PROJECT_NAME}_test)
