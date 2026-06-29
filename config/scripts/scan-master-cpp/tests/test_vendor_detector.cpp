// ============================================================
//  test_vendor_detector.cpp — Unit tests for vendor detection
//
//  These tests verify the scoring engine without needing a
//  real scanner or OCR pipeline. We create synthetic documents
//  with known text and verify the detector picks the right vendor.
//
//  Build with: cmake -DBUILD_TESTS=ON ..
//  Run with: ./build/scan_master_tests
// ============================================================

#include "../src/vendor_detector.h"
#include "../src/vendor_config.h"
#include "../src/types.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace scan_master;

// ── Helper: create a minimal VendorProfile ──────────────────
// (disabled — using full registry from vendor files instead)

VendorProfile make_profile(const std::string& name,
                            std::vector<std::pair<std::string, int>> keywords) {
    VendorProfile p;
    p.name = name;
    for (auto& [pattern, weight] : keywords) {
        KeywordClue kw;
        kw.pattern = pattern;
        kw.weight = weight;
        kw.is_regex = false;
        p.keywords.push_back(kw);
    }
    p.default_subtype = "Invoice";
    p.file_naming.normal_template = "{vendor}_{invoice}.pdf";
    p.file_naming.unknown_template = "{vendor}_UNKNOWN_{date}_{time}.pdf";
    return p;
}

// ── Helper: create a Document with text ────────────────────

Document make_doc(const std::string& text, int page = 1) {
    Document doc;
    doc.text = text;
    doc.pdf_path = "/tmp/test.pdf";
    doc.page_number = page;
    return doc;
}

// ── Tests ──────────────────────────────────────────────────

void test_autozone_keyword_detection() {
    VendorRegistry registry;
    registry.load_directory("../vendors");

    auto doc = make_doc("AutoZone Commercial Invoice\nInvoice Number: 02227123456\nTotal: $45.67");
    auto result = detect_vendor(doc, registry);

    assert(result.vendor_name == "AutoZone");
    assert(result.score >= 3);
    assert(!result.is_unknown());
    std::cout << "✓ AutoZone keyword detection works" << std::endl;
}

void test_napa_keyword_detection() {
    VendorRegistry registry;
    registry.load_directory("../vendors");

    auto doc = make_doc("NAPA Auto Parts\nLawrence's Auto Parts\nInvoice Number: 12345");
    auto result = detect_vendor(doc, registry);

    assert(result.vendor_name == "NAPA");
    assert(result.score >= 3);
    std::cout << "✓ NAPA keyword detection works" << std::endl;
}

void test_unknown_document() {
    VendorRegistry registry;
    registry.load_directory("../vendors");

    auto doc = make_doc("Random gibberish that doesn't match any vendor at all");
    auto result = detect_vendor(doc, registry);

    assert(result.is_unknown());
    std::cout << "✓ Unknown document correctly identified" << std::endl;
}

void test_context_phone_fallback() {
    VendorRegistry registry;
    registry.load_directory("../vendors");

    // No keywords, but has a known phone number
    auto doc = make_doc("Some random document\nCall us at 503-682-1777 for details");
    auto result = detect_vendor(doc, registry);

    assert(result.vendor_name == "TEC");
    assert(result.used_context_fallback);
    std::cout << "✓ Context phone fallback works" << std::endl;
}

void test_context_domain_fallback() {
    VendorRegistry registry;
    registry.load_directory("../vendors");

    // No keywords, but has a known domain
    auto doc = make_doc("Visit autozonepro.com for more info");
    auto result = detect_vendor(doc, registry);

    assert(result.vendor_name == "AutoZone");
    assert(result.used_context_fallback);
    std::cout << "✓ Context domain fallback works" << std::endl;
}

void test_scoring_picks_highest() {
    VendorRegistry registry;
    registry.load_directory("../vendors");

    // This text matches both AutoZone (keyword) and has TEC phone
    // AutoZone should win because keyword score > context score
    auto doc = make_doc("AutoZone Invoice\nPhone: 503-682-1777\nInvoice Number: 02227111111");
    auto result = detect_vendor(doc, registry);

    assert(result.vendor_name == "AutoZone");
    assert(!result.used_context_fallback);
    std::cout << "✓ Scoring correctly picks highest (AutoZone over TEC)" << std::endl;
}

void test_empty_registry() {
    VendorRegistry registry;
    // No vendors loaded

    auto doc = make_doc("AutoZone Invoice");
    auto result = detect_vendor(doc, registry);

    assert(result.is_unknown());
    assert(result.score == 0);
    std::cout << "✓ Empty registry returns UNKNOWN" << std::endl;
}

// ── Main ───────────────────────────────────────────────────

int main() {
    std::cout << "Running vendor detector tests..." << std::endl;
    std::cout << std::endl;

    test_autozone_keyword_detection();
    test_napa_keyword_detection();
    test_unknown_document();
    test_context_phone_fallback();
    test_context_domain_fallback();
    test_scoring_picks_highest();
    test_empty_registry();

    std::cout << std::endl;
    std::cout << "All tests passed! ✓" << std::endl;
    return 0;
}
