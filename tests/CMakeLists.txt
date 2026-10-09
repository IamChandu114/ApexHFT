find_package(GTest CONFIG REQUIRED)

add_executable(apexhft_tests
    test_version.cpp
    test_domain.cpp
    test_matching_engine.cpp
    test_replay.cpp
    test_risk.cpp
    test_oms.cpp
    test_oms_external.cpp
    test_event_log.cpp
    test_oms_audit.cpp
    test_oms_replay.cpp
    test_research.cpp
)

target_link_libraries(apexhft_tests
    PRIVATE
        apexhft_core
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(apexhft_tests)

