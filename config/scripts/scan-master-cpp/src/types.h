// ============================================================
//  types.h — Core data types for Scan Master
//
//  All modules share these types. Keeping them in one place
//  avoids circular dependencies and gives us a single source
//  of truth for the data flowing through the pipeline.
//
//  Design notes for B:
//  - VendorProfile is the central data structure. Everything
//    revolves around it. One struct = one vendor.
//  - Document is the output of the Pipeline stage. It's what
//    the detector and extractor operate on.
//  - DetectionResult and InvoiceInfo are value types returned
//    by the processing stages. No out-params, no globals.
// ============================================================

#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <regex>
#include <cstdint>

namespace scan_master {

// ── Forward declarations ──────────────────────────────────
struct VendorProfile;
struct Document;
struct DetectionResult;
struct InvoiceInfo;

// ── Vendor detection ───────────────────────────────────────

/**
 * A single keyword clue for detecting a vendor.
 * The detector checks if `pattern` appears in the document text
 * (case-insensitive). If found, it adds `weight` to that vendor's score.
 *
 * `is_regex` controls whether `pattern` is treated as a regex
 * or a plain substring match. Regex is more powerful but slower
 * — use plain strings when they're sufficient.
 */
struct KeywordClue {
    std::string pattern;    // Text or regex to search for
    int weight = 1;         // Points added to score on match
    bool is_regex = false;  // true = treat pattern as regex
};

/**
 * Context clues are structural signals that a document belongs
 * to a vendor, even when keyword matching fails.
 * These are the "learning" data — phone numbers, domains,
 * addresses, city+zip combos that appear on the document.
 *
 * They're stored in the vendor JSON files and updated by
 * the context learner when new mappings are discovered.
 */
struct ContextClue {
    std::map<std::string, std::string> phones;     // "503-371-1648" → vendor
    std::map<std::string, std::string> domains;    // "autozonepro.com" → vendor
    std::map<std::string, std::string> cities;     // "wilsonville|97070" → vendor
    std::map<std::string, std::string> addresses;  // "7950 SW Burns Way" → vendor
};

/**
 * Invoice extraction strategy for a vendor.
 * Each vendor has its own regex patterns for pulling the
 * invoice number out of OCR text. Some vendors need
 * multiple fallback strategies (like AutoZone).
 */
struct InvoiceExtraction {
    std::vector<std::string> patterns;  // Regex patterns, tried in order
    std::string validation_regex;      // Final sanity check on extracted number
};

/**
 * Filename template for a vendor.
 * Uses placeholders:
 *   {vendor}  — vendor name (e.g. "AutoZone")
 *   {invoice} — extracted invoice number
 *   {subtype} — document subtype (e.g. "ARCredit", "LaborClaim")
 *   {date}    — current date (YYYYMMDD)
 *   {time}    — current time (HHMMSS)
 */
struct FileNaming {
    std::string normal_template;    // e.g. "{vendor}_{invoice}.pdf"
    std::string unknown_template;  // e.g. "{vendor}_UNKNOWN_{date}_{time}.pdf"
};

/**
 * The complete profile for one vendor.
 * This is what gets loaded from the vendor JSON files.
 * Adding a new vendor = creating a new JSON file, zero code changes.
 */
struct VendorProfile {
    std::string name;                       // "AutoZone", "NAPA", etc.
    std::vector<KeywordClue> keywords;       // Scoring keywords
    ContextClue context;                    // Phone/domain/city/address clues
    InvoiceExtraction invoice_extraction;    // How to extract invoice numbers
    FileNaming file_naming;                 // How to name the output file
    std::string default_subtype;            // Subtype when none detected (e.g. "Invoice")
};

// ── Pipeline output ────────────────────────────────────────

/**
 * A document produced by the scanning pipeline.
 * This is the input to vendor detection and invoice extraction.
 *
 * The pipeline produces one Document per scanned page.
 * Each Document carries both the normalized PDF path and
 * the extracted plain text.
 */
struct Document {
    std::string pdf_path;           // Path to letter-size normalized PDF
    std::string text;               // OCR text (layout-preserving)
    std::string text_no_layout;     // OCR text (reflowed, sometimes cleaner)
    std::string source_tiff;        // Original scanned TIFF
    int page_number = 0;            // Page number within this scan batch
};

// ── Detection result ───────────────────────────────────────

/**
 * Result of vendor detection for a single document.
 * Contains the winning vendor, its score, and which clues
 * matched (for debugging/logging).
 */
struct DetectionResult {
    std::string vendor_name;           // Best-matched vendor, or "UNKNOWN"
    int score = 0;                     // Score of best match
    int threshold = 2;                 // Minimum score to accept a match
    std::vector<std::string> matched_clues;  // Which clues fired (for logging)
    bool used_context_fallback = false; // True if keyword score < threshold

    bool is_confident() const { return score >= threshold && vendor_name != "UNKNOWN"; }
    bool is_unknown() const { return vendor_name == "UNKNOWN" || score < threshold; }
};

// ── Invoice extraction result ──────────────────────────────

/**
 * Result of invoice number extraction.
 * The extracted number, subtype (if detected), and whether
 * the number passed validation.
 */
struct InvoiceInfo {
    std::string vendor_name;           // Which vendor this was extracted for
    std::string invoice_number;        // Extracted invoice number (may be empty)
    std::string subtype;               // "Invoice", "ARCredit", "LaborClaim", etc.
    bool valid = false;                // Passed validation_regex check
    std::string source_clue;           // Which clue produced this (for logging)
};

// ── Utility ────────────────────────────────────────────────

/**
 * Execution result from running an external command.
 * We capture both exit code and stdout/stderr so callers
 * can make real decisions instead of hoping for the best.
 */
struct ExecResult {
    int exit_code = -1;
    std::string stdout;
    std::string stderr;

    bool ok() const { return exit_code == 0; }
};

} // namespace scan_master
