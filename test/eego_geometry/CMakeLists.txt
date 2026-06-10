add_executable(EegoGeometryTest
  EegoGeometryTest.cpp
)

target_link_libraries(EegoGeometryTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(EegoGeometryTest)
