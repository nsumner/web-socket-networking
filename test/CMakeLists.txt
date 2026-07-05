include(FetchContent)

# Use an installed GoogleTest if it is new enough; otherwise download a copy.
FetchContent_Declare(googletest
  URL      https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
  URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
  SYSTEM
  EXCLUDE_FROM_ALL
  FIND_PACKAGE_ARGS 1.14 NAMES GTest
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Third-party code has its own warning policies.
set(_networking_saved_warn "${CMAKE_COMPILE_WARNING_AS_ERROR}")
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
FetchContent_MakeAvailable(googletest)
set(CMAKE_COMPILE_WARNING_AS_ERROR "${_networking_saved_warn}")

add_executable(networking-tests
  EndToEndTests.cpp
  ScheduleFuzzTests.cpp
  TeardownTests.cpp
)

target_compile_features(networking-tests PRIVATE cxx_std_23)
networking_apply_options(networking-tests)

target_link_libraries(networking-tests
  PRIVATE
    WebSocketNetworking::networking
    GTest::gtest_main
)

include(GoogleTest)
# Discovery runs the binary post-link. Sanitized builds start slowly enough
# that the default 5s timeout can flake. Maybe look into a more robust way.
gtest_discover_tests(networking-tests
  DISCOVERY_TIMEOUT 30
)
