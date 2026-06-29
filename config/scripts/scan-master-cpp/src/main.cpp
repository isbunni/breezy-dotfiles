// ============================================================
//  main.cpp — scan-master entry point
//
//  Orchestrates the full scan workflow:
//    1. Load vendor profiles
//    2. Run the scanning pipeline
//    3. Detect vendor for each page
//    4. Extract invoice numbers
//    5. Save with proper filenames
//    6. Learn context clues
//
//  This is the conductor — it wires together the pipeline,
//  detector, extractor, and learner. All the logic lives
//  in those modules. This file just sequences the steps.
// ============================================================

#include "pipeline.h"
#include "vendor_config.h"
#include "vendor_detector.h"
#include "invoice_extractor.h"
#include "file_namer.h"
#include "context_learner.h"
#include "utils.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace scan_master;
namespace fs = std::filesystem;

// ── Configuration ───────────────────────────────────────────
// In a future version, these could come from CLI args or a config file.
// For now, they're constants that match your setup.

static const fs::path SAVE_DIR = [] {
    // Use OneDrive save dir if it exists, fall back to local
    auto path = fs::path(std::getenv("HOME")) / "OneDrive" / "Desktop" / "SCANS 2026";
    if (fs::exists(path)) return path;
    // Fallback for non-OneDrive systems
    return fs::path(std::getenv("HOME")) / "Desktop" / "SCANS 2026";
}();

static const fs::path VENDOR_DIR = [] {
    // Look for vendor files in the source tree first,
    // then fall back to user config dir
    auto source_dir = fs::path(std::getenv("HOME")) / "breezy-dotfiles" / "config" / "scripts" / "scan-master-cpp" / "vendors";
    if (fs::exists(source_dir)) return source_dir;
    return fs::path(std::getenv("HOME")) / ".config" / "scan-master" / "vendors";
}();

static const PipelineConfig PIPELINE_CONFIG;

// ── Main scan workflow ─────────────────────────────────────

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Scan Master C++ — McFarlands Mobile Mechanics" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::endl;

    // Ensure the save directory exists
    ensure_dir(SAVE_DIR);

    // Step 1: Load vendor profiles
    VendorRegistry registry;
    registry.load_directory(VENDOR_DIR);

    if (registry.size() == 0) {
        std::cerr << "ERROR: No vendor profiles loaded. Check " << VENDOR_DIR << std::endl;
        return 1;
    }

    // Step 2: Run the scanning pipeline
    std::vector<Document> documents;
    try {
        documents = run_pipeline(PIPELINE_CONFIG, SAVE_DIR);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Pipeline failed: " << e.what() << std::endl;
        return 1;
    }

    if (documents.empty()) {
        std::cerr << "ERROR: No documents produced by pipeline." << std::endl;
        return 1;
    }

    // Step 3: Process each document
    int total_saved = 0;

    for (auto& doc : documents) {
        std::cout << "── Page " << doc.page_number << " ──────────────────────────────────" << std::endl;

        // Check if we got any text
        if (doc.text.empty()) {
            std::cout << "  ⚠ No text extracted — saving as UNKNOWN" << std::endl;
            auto out = generate_unknown_filename(SAVE_DIR);
            copy_file(doc.pdf_path, out);
            std::cout << "  → " << out.filename().string() << std::endl;
            std::cout << std::endl;
            total_saved++;
            continue;
        }

        // Step 3a: Detect vendor
        auto detection = detect_vendor(doc, registry);

        // Step 3b: Extract invoice
        InvoiceInfo invoice;
        if (!detection.is_unknown()) {
            const auto* vendor = registry.find(detection.vendor_name);
            if (vendor) {
                invoice = extract_invoice(doc, *vendor);
            }
        }

        // Step 3c: Generate filename and save
        fs::path out;
        if (detection.is_unknown()) {
            out = generate_unknown_filename(SAVE_DIR);
        } else {
            const auto* vendor = registry.find(detection.vendor_name);
            if (vendor) {
                out = generate_filename(*vendor, invoice, SAVE_DIR);
            } else {
                out = generate_unknown_filename(SAVE_DIR);
            }
        }

        copy_file(doc.pdf_path, out);
        std::cout << "  → " << out.filename().string() << std::endl;
        std::cout << std::endl;
        total_saved++;

        // Step 3d: Learn context clues (if not UNKNOWN)
        if (!detection.is_unknown() && detection.vendor_name != "UNKNOWN") {
            learn_from_document(doc, detection.vendor_name, registry, VENDOR_DIR);
        }
    }

    // Summary
    int total_files = 0;
    for (const auto& entry : fs::directory_iterator(SAVE_DIR)) {
        if (entry.path().extension() == ".pdf") total_files++;
    }

    std::cout << "============================================" << std::endl;
    std::cout << "  Done! " << documents.size() << " page(s) processed." << std::endl;
    std::cout << "  Total files in SCANS 2026: " << total_files << std::endl;
    std::cout << "============================================" << std::endl;

    return 0;
}
