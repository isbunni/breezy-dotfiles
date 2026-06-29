// ============================================================
//  main_learn.cpp — scan-learn entry point
//
//  Teaches the system from a manually-corrected file.
//  Extracts clues from the PDF and saves them to the
//  vendor's JSON file.
//
//  Usage:
//    scan-learn <pdf_file> <VendorName>
//    scan-learn UNKNOWN_20260625_104456.pdf TEC
//
//  This is the C++ equivalent of scan-learn.sh.
// ============================================================

#include "pipeline.h"
#include "vendor_config.h"
#include "context_learner.h"
#include "utils.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace scan_master;

namespace fs = std::filesystem;

static const fs::path VENDOR_DIR = [] {
    auto install_dir = fs::path(std::getenv("HOME")) / ".config" / "scripts" / "scan-master-cpp" / "vendors";
    if (fs::exists(install_dir)) return install_dir;
    auto source_dir = fs::path(std::getenv("HOME")) / "breezy-dotfiles" / "config" / "scripts" / "scan-master-cpp" / "vendors";
    if (fs::exists(source_dir)) return source_dir;
    return install_dir;
}();

static const fs::path SCANS_DIR = [] {
    auto path = fs::path(std::getenv("HOME")) / "OneDrive" / "Desktop" / "SCANS 2026";
    if (fs::exists(path)) return path;
    return fs::path(std::getenv("HOME")) / "Desktop" / "SCANS 2026";
}();

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: scan-learn <pdf_file> <VendorName>" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Examples:" << std::endl;
        std::cerr << "  scan-learn UNKNOWN_20260625_104456.pdf TEC" << std::endl;
        std::cerr << "  scan-learn SomeFile.pdf NewVendor" << std::endl;
        return 1;
    }

    std::string pdf_arg = argv[1];
    std::string vendor_name = argv[2];

    // Resolve the PDF path
    fs::path pdf_path = pdf_arg;
    if (!fs::exists(pdf_path)) {
        // Try in the SCANS directory
        pdf_path = SCANS_DIR / pdf_arg;
    }
    if (!fs::exists(pdf_path)) {
        std::cerr << "ERROR: File not found: " << pdf_arg << std::endl;
        return 1;
    }

    // Load vendor registry
    VendorRegistry registry;
    registry.load_directory(VENDOR_DIR);

    // Learn from the PDF
    std::cout << "📖 Learning from: " << pdf_path.filename().string() << std::endl;
    std::cout << "   Vendor: " << vendor_name << std::endl;
    std::cout << std::endl;

    int learned = learn_from_pdf(pdf_path, vendor_name, registry, VENDOR_DIR);

    if (learned < 0) {
        return 1;
    }

    std::cout << std::endl;
    if (learned > 0) {
        std::cout << "✅ Learned " << learned << " new clue(s) for "
                  << vendor_name << "." << std::endl;
        std::cout << "   Future scans will use these to identify "
                  << vendor_name << " invoices." << std::endl;
    } else {
        std::cout << "ℹ No new clues found — everything was already known." << std::endl;
    }

    return 0;
}
