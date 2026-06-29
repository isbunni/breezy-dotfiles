# ============================================================
#  Scan Master C++ — McFarlands Mobile Mechanics
#  Refactored from scan-master.sh (bash) to C++20.
#
#  Architecture: data-driven registry pattern.
#  - Pipeline: scan → OCR → normalize → extract text
#  - VendorRegistry: loads vendor profiles from JSON files
#  - VendorDetector: scores each vendor against document text
#  - InvoiceExtractor: per-vendor regex-based extraction
#  - ContextLearner: learns phone/domain/city → vendor mappings
#
#  Build:
#    mkdir build && cd build
#    cmake .. -DCMAKE_BUILD_TYPE=Release
#    make -j$(nproc)
#
#  Dependencies:
#    - nlohmann/json (header-only, bundled in third_party/)
#    - fmt (or std::format — using std::format for C++20)
#    - System tools: scanimage, ocrmypdf, gs, pdftotext
# ============================================================

cmake_minimum_required(VERSION 3.20)
project(scan-master-cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── nlohmann/json ──────────────────────────────────────────
# Header-only library, bundled in third_party/nlohmann/
# If you'd rather use a system package, replace this with:
#   find_package(nlohmann_json 3.11.0 REQUIRED)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann/include
)

# ── Library: scan_master_lib ──────────────────────────────
# All core logic lives here so it's testable and reusable.
add_library(scan_master_lib STATIC
    src/document.cpp
    src/pipeline.cpp
    src/vendor_config.cpp
    src/vendor_detector.cpp
    src/invoice_extractor.cpp
    src/file_namer.cpp
    src/context_learner.cpp
    src/utils.cpp
)
target_include_directories(scan_master_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(scan_master_lib PUBLIC nlohmann_json)

# ── scan-master: main binary ───────────────────────────────
add_executable(scan-master src/main.cpp)
target_link_libraries(scan-master PRIVATE scan_master_lib)

# ── scan-invoice: AutoZone quick-scan binary ───────────────
add_executable(scan-invoice src/main_invoice.cpp)
target_link_libraries(scan-invoice PRIVATE scan_master_lib)

# ── scan-learn: vendor teaching tool ───────────────────────
add_executable(scan-learn src/main_learn.cpp)
target_link_libraries(scan-learn PRIVATE scan_master_lib)

# ── Tests ──────────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_executable(scan_master_tests
        tests/test_vendor_detector.cpp
        tests/test_invoice_extractor.cpp
        tests/test_pipeline.cpp
    )
    target_link_libraries(scan_master_tests PRIVATE scan_master_lib GTest::GTest)
    add_test(NAME scan_master_tests COMMAND scan_master_tests)
endif()
