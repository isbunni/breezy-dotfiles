// ============================================================
//  main_invoice.cpp — scan-invoice entry point
//
//  Quick-scan for AutoZone invoices only.
//  This is a simplified version of scan-master that:
//    1. Scans from the ADF
//    2. OCRs and normalizes
//    3. Only checks for AutoZone (fast path)
//    4. Saves with AutoZone naming convention
//
//  Use this when you know you're scanning AutoZone docs
//  and don't want the overhead of checking all vendors.
// ============================================================

#include "pipeline.h"
#include "vendor_config.h"
#include "vendor_detector.h"
#include "invoice_extractor.h"
#include "file_namer.h"
#include "utils.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace scan_master;

namespace fs = std::filesystem;

static const fs::path SAVE_DIR = [] {
    auto path = fs::path(std::getenv("HOME")) / "OneDrive" / "Desktop" / "SCANS 2026";
    if (fs::exists(path)) return path;
    return fs::path(std::getenv("HOME")) / "Desktop" / "SCANS 2026";
}();

static const fs::path VENDOR_DIR = [] {
    auto install_dir = fs::path(std::getenv("HOME")) / ".config" / "scripts" / "scan-master-cpp" / "vendors";
    if (fs::exists(install_dir)) return install_dir;
    auto source_dir = fs::path(std::getenv("HOME")) / "breezy-dotfiles" / "config" / "scripts" / "scan-master-cpp" / "vendors";
    if (fs::exists(source_dir)) return source_dir;
    return install_dir;
}();

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Scan Invoice — AutoZone Only" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::endl;

    ensure_dir(SAVE_DIR);

    // Load vendor profiles (we need AutoZone's)
    VendorRegistry registry;
    registry.load_directory(VENDOR_DIR);

    const auto* autozone = registry.find("AutoZone");
    if (!autozone) {
        std::cerr << "ERROR: AutoZone vendor profile not found." << std::endl;
        return 1;
    }

    // Run the pipeline
    PipelineConfig config;
    std::vector<Document> documents;
    try {
        documents = run_pipeline(config, SAVE_DIR);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Pipeline failed: " << e.what() << std::endl;
        return 1;
    }

    // Process each page — AutoZone only
    for (auto& doc : documents) {
        std::cout << "── Page " << doc.page_number << " ──────────────────────────────────" << std::endl;

        if (doc.text.empty()) {
            std::cout << "  ⚠ No text — saving as UNKNOWN" << std::endl;
            auto out = generate_unknown_filename(SAVE_DIR);
            copy_file(doc.pdf_path, out);
            std::cout << "  → " << out.filename().string() << std::endl;
            std::cout << std::endl;
            continue;
        }

        // Quick AutoZone check
        bool is_autozone = false;
        if (contains_ci(doc.text, "autozone") || contains_ci(doc.text, "autozonepro") ||
            contains_ci(doc.text, "AutoZonePro.com")) {
            is_autozone = true;
        }
        // Also check for 11-digit numbers starting with 022
        auto numbers = regex_find_all(doc.text, R"(\b0\d{10}\b)");
        if (!numbers.empty()) is_autozone = true;

        if (!is_autozone) {
            std::cout << "  ⚠ Not recognized as AutoZone — saving as UNKNOWN" << std::endl;
            auto out = generate_unknown_filename(SAVE_DIR);
            copy_file(doc.pdf_path, out);
            std::cout << "  → " << out.filename().string() << std::endl;
            std::cout << std::endl;
            continue;
        }

        // Extract invoice and save
        auto invoice = extract_invoice(doc, *autozone);
        auto out = generate_filename(*autozone, invoice, SAVE_DIR);
        copy_file(doc.pdf_path, out);
        std::cout << "  → " << out.filename().string() << std::endl;
        std::cout << std::endl;
    }

    std::cout << "============================================" << std::endl;
    std::cout << "  Done! " << documents.size() << " page(s) processed." << std::endl;
    std::cout << "============================================" << std::endl;

    return 0;
}
