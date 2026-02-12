# ============================================================
# 테스트 빌드 설정 — Google Test 기반 단위 테스트
# ============================================================
# 사용법:
#   cmake -DBUILD_TESTS=ON .. && make -j$(nproc) && ctest --output-on-failure
#
# 테스트 실행 파일:
#   - test_security  : 보안 모듈 테스트 (15개 케이스)
#   - test_rendering : 렌더링 엔진 테스트 (16개 케이스)
#   - test_network   : 네트워크 레이어 테스트 (10개 케이스)
# ============================================================

cmake_minimum_required(VERSION 3.20)

# ---- Google Test 의존성 ----
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
# Windows에서 gtest가 공유 런타임과 충돌하지 않도록 설정
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Google Test 매크로 활성화
include(GoogleTest)

# ---- 공통 설정 ----
set(TEST_COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/network
    ${CMAKE_SOURCE_DIR}/src/security
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/ui
)

# 프로젝트 소스 파일 (테스트 대상)
file(GLOB SECURITY_SOURCES
    ${CMAKE_SOURCE_DIR}/src/security/*.cpp
)
file(GLOB RENDERING_SOURCES
    ${CMAKE_SOURCE_DIR}/src/rendering/*.cpp
)
file(GLOB NETWORK_SOURCES
    ${CMAKE_SOURCE_DIR}/src/network/*.cpp
)

# ============================================================
# 1. 보안 모듈 테스트 (15개 케이스)
# ============================================================
add_executable(test_security
    test_security.cpp
    ${SECURITY_SOURCES}
)
target_include_directories(test_security PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(test_security PRIVATE
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)
target_compile_features(test_security PRIVATE cxx_std_20)
gtest_discover_tests(test_security
    PROPERTIES LABELS "security"
    DISCOVERY_TIMEOUT 30
)

# ============================================================
# 2. 렌더링 엔진 테스트 (16개 케이스)
# ============================================================
add_executable(test_rendering
    test_rendering.cpp
    ${RENDERING_SOURCES}
)
target_include_directories(test_rendering PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(test_rendering PRIVATE
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)
target_compile_features(test_rendering PRIVATE cxx_std_20)
gtest_discover_tests(test_rendering
    PROPERTIES LABELS "rendering"
    DISCOVERY_TIMEOUT 30
)

# ============================================================
# 3. 네트워크 레이어 테스트 (10개 케이스)
# ============================================================
add_executable(test_network
    test_network.cpp
    ${NETWORK_SOURCES}
)
target_include_directories(test_network PRIVATE ${TEST_COMMON_INCLUDES})
target_link_libraries(test_network PRIVATE
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)
target_compile_features(test_network PRIVATE cxx_std_20)
gtest_discover_tests(test_network
    PROPERTIES LABELS "network"
    DISCOVERY_TIMEOUT 30
)

# ============================================================
# 커스텀 테스트 타겟 — 전체 테스트 한 번에 실행
# ============================================================
add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j4
    DEPENDS test_security test_rendering test_network
    COMMENT "🧪 전체 C++ 테스트 실행 중..."
)
