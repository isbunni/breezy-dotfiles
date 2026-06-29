// ============================================================
//  pipeline.h — Scanning pipeline
//
//  Orchestrates the physical scanning and OCR process:
//    1. scanimage — scan pages from the Brother ADF
//    2. ocrmypdf — OCR the scanned TIFFs into searchable PDFs
//    3. gs — normalize PDFs to letter size (612x792 pts)
//    4. pdftotext — extract plain text from the PDFs
//
//  Each scanned page becomes a Document struct containing
//  both the PDF path and extracted text.
//
//  The pipeline is tool-agnostic — it calls external commands
//  via exec_command(). If any tool fails, it throws (unlike the
//  bash version which just piped to /dev/null and hoped).
// ============================================================

#pragma once

#include "types.h"
#include <filesystem>
#include <vector>

namespace scan_master {

/**
 * Configuration for the scanning pipeline.
 * Defaults match the Brother ADF scanner at McFarlands.
 */
struct PipelineConfig {
    std::string device = "brother4:net1;dev0";
    int resolution = 300;  // DPI
    int timeout_seconds = 120;  // Per-command timeout
};

/**
 * Scan pages from the ADF and produce raw TIFF files.
 * Returns the number of pages scanned.
 *
 * Throws if scanimage fails or produces no output.
 */
int scan_pages(const PipelineConfig& config, const std::filesystem::path& tmp_dir);

/**
 * OCR a TIFF file into a searchable PDF.
 * Input: path to TIFF file
 * Output: path where the OCR'd PDF will be written
 *
 * Throws if ocrmypdf fails.
 */
void ocr_page(const std::filesystem::path& tiff_path,
              const std::filesystem::path& output_pdf,
              int timeout_seconds = 120);

/**
 * Normalize a PDF to letter size (8.5" x 11" = 612 x 792 pts).
 * This ensures consistent page dimensions regardless of
 * what the scanner detected.
 *
 * Throws if ghostscript fails.
 */
void normalize_pdf(const std::filesystem::path& input_pdf,
                   const std::filesystem::path& output_pdf,
                   int timeout_seconds = 60);

/**
 * Extract text from a PDF using pdftotext.
 * Returns the extracted text as a string.
 *
 * Two extractions are done: one with -layout (preserves
 * positioning) and one without (reflowed). Some vendors
 * match better with one or the other.
 *
 * Throws if pdftotext fails.
 */
struct ExtractedText {
    std::string layout;
    std::string no_layout;
};

ExtractedText extract_text(const std::filesystem::path& pdf_path,
                           int timeout_seconds = 60);

/**
 * Run the full pipeline: scan → OCR → normalize → extract.
 * Returns one Document per scanned page.
 *
 * This is the main entry point for the scan operation.
 */
std::vector<Document> run_pipeline(const PipelineConfig& config,
                                    const std::filesystem::path& save_dir);

} // namespace scan_master
