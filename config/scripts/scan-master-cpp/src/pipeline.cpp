// ============================================================
//  pipeline.cpp — Scanning pipeline implementation
//
//  This is the C++ equivalent of the bash pipeline:
//    scanimage → ocrmypdf → gs → pdftotext
//
//  Each stage checks for errors. In the bash version, failures
//  were silently swallowed with 2>/dev/null. Here, we throw
//  on failure so the caller can actually respond to problems.
// ============================================================

#include "pipeline.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace scan_master {

// ── Step 1: Scan from ADF ──────────────────────────────────

int scan_pages(const PipelineConfig& config, const std::filesystem::path& tmp_dir) {
    std::cout << "[Pipeline] Scanning from ADF at " << config.resolution << "dpi..." << std::endl;

    // Build the scanimage command.
    // --batch with --batch-count=99 means "scan up to 99 pages,
    // stopping when the ADF is empty."
    std::ostringstream cmd;
    cmd << "scanimage"
        << " --device-name=\"" << config.device << "\""
        << " --resolution=" << config.resolution
        << " --format=tiff"
        << " --batch=\"" << tmp_dir.string() << "/scan_%03d.tiff\""
        << " --batch-count=99"
        << " --source=\"Automatic Document Feeder(left aligned)\""
        << " -x 215.9 -y 279.4";

    // scanimage returns non-zero when the batch terminates early
    // (ADF empty). That's OK — we just need at least one page.
    auto result = exec_command(cmd.str(), config.timeout_seconds);

    // Count how many TIFFs were produced
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(tmp_dir)) {
        if (entry.path().extension() == ".tiff") {
            count++;
        }
    }

    if (count == 0) {
        std::cerr << "[Pipeline] stderr: " << result.stderr << std::endl;
        throw std::runtime_error("No pages were scanned. Check the scanner.");
    }

    std::cout << "[Pipeline] Scanned " << count << " page(s)." << std::endl;
    return count;
}

// ── Step 2: OCR ────────────────────────────────────────────

void ocr_page(const std::filesystem::path& tiff_path,
              const std::filesystem::path& output_pdf,
              int timeout_seconds) {
    std::ostringstream cmd;
    cmd << "ocrmypdf"
        << " --quiet"
        << " --force-ocr"
        << " --output-type pdf"
        << " \"" << tiff_path.string() << "\""
        << " \"" << output_pdf.string() << "\"";

    auto result = exec_command(cmd.str(), timeout_seconds);

    if (!result.ok()) {
        // ocrmypdf warnings go to stderr but the file may still be OK.
        // Only fail if the output file doesn't exist.
        if (!std::filesystem::exists(output_pdf)) {
            std::cerr << "[Pipeline] ocrmypdf stderr: " << result.stderr << std::endl;
            throw std::runtime_error("OCR failed for " + tiff_path.string());
        }
    }
}

// ── Step 3: Normalize to letter size ───────────────────────

void normalize_pdf(const std::filesystem::path& input_pdf,
                   const std::filesystem::path& output_pdf,
                   int timeout_seconds) {
    std::ostringstream cmd;
    cmd << "gs"
        << " -dNOPAUSE -dBATCH"
        << " -sDEVICE=pdfwrite"
        << " -dDEVICEWIDTHPOINTS=612"
        << " -dDEVICEHEIGHTPOINTS=792"
        << " -dFIXEDMEDIA"
        << " -dPDFFitPage"
        << " -sOutputFile=\"" << output_pdf.string() << "\""
        << " \"" << input_pdf.string() << "\"";

    auto result = exec_command(cmd.str(), timeout_seconds);

    if (!result.ok() || !std::filesystem::exists(output_pdf)) {
        std::cerr << "[Pipeline] gs stderr: " << result.stderr << std::endl;
        throw std::runtime_error("PDF normalization failed for " + input_pdf.string());
    }
}

// ── Step 4: Extract text ───────────────────────────────────

ExtractedText extract_text(const std::filesystem::path& pdf_path,
                           int timeout_seconds) {
    ExtractedText result;

    // Layout-preserving extraction
    {
        std::ostringstream cmd;
        cmd << "pdftotext -layout \"" << pdf_path.string() << "\" -";
        auto exec_result = exec_command(cmd.str(), timeout_seconds);
        result.layout = exec_result.stdout;
    }

    // Reflowed extraction (sometimes cleaner for matching)
    {
        std::ostringstream cmd;
        cmd << "pdftotext \"" << pdf_path.string() << "\" -";
        auto exec_result = exec_command(cmd.str(), timeout_seconds);
        result.no_layout = exec_result.stdout;
    }

    return result;
}

// ── Full pipeline ──────────────────────────────────────────

std::vector<Document> run_pipeline(const PipelineConfig& config,
                                    const std::filesystem::path& /*save_dir*/) {
    // Create a temporary directory for intermediate files.
    // It's automatically cleaned up when this function returns.
    TempDir tmp_dir;
    std::cout << "[Pipeline] Working in " << tmp_dir.path() << std::endl;

    // Step 1: Scan
    int page_count = scan_pages(config, tmp_dir.path());

    // Steps 2-4: Process each page
    std::vector<Document> documents;

    for (int i = 1; i <= page_count; i++) {
        // Build the TIFF filename (matches scanimage's --batch pattern)
        std::ostringstream tiff_name;
        tiff_name << "scan_" << std::setw(3) << std::setfill('0') << i << ".tiff";
        auto tiff_path = tmp_dir.path() / tiff_name.str();

        if (!std::filesystem::exists(tiff_path)) {
            std::cerr << "[Pipeline] Warning: expected TIFF not found: " << tiff_path << std::endl;
            continue;
        }

        std::cout << "[Pipeline] Processing page " << i << "..." << std::endl;

        // Step 2: OCR
        auto ocr_pdf = tmp_dir.path() / (std::string("scan_") + std::to_string(i) + ".pdf");
        ocr_page(tiff_path, ocr_pdf);

        // Step 3: Normalize
        auto letter_pdf = tmp_dir.path() / (std::string("scan_") + std::to_string(i) + "_letter.pdf");
        normalize_pdf(ocr_pdf, letter_pdf);

        // Step 4: Extract text
        auto text = extract_text(letter_pdf);

        // Build the Document
        Document doc;
        doc.pdf_path = letter_pdf.string();
        doc.text = std::move(text.layout);
        doc.text_no_layout = std::move(text.no_layout);
        doc.source_tiff = tiff_path.string();
        doc.page_number = i;

        documents.push_back(std::move(doc));
    }

    // Copy the final PDFs to the save directory.
    // We do this here (not in the main app) so the pipeline
    // is testable without a real save directory.
    // Actually — let the caller decide where to save. We return
    // the Documents with their temp paths, and the caller copies.

    std::cout << "[Pipeline] Done. " << documents.size() << " document(s) produced." << std::endl;
    return documents;
}

} // namespace scan_master
