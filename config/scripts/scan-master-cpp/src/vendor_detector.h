// ============================================================
//  vendor_detector.h — Scoring-based vendor identification
//
//  This is the brain of the system. It takes a Document (with
//  extracted text) and scores each vendor in the registry
//  against that text.
//
//  Detection happens in two passes:
//    Pass 1: Keyword matching. Each vendor's keywords are checked
//            against the document text. Matches add weight to the
//            vendor's score.
//    Pass 2: Context fallback. If no vendor scored above the
//            threshold, we check context clues (phone numbers,
//            domains, cities, addresses) for a match.
//
//  The vendor with the highest score wins. If the highest score
//  is below the threshold, the document is classified as UNKNOWN.
//
//  This is a stateless service — it takes a Document and
//  VendorRegistry, returns a DetectionResult. No globals, no
//  side effects. Easy to test.
// ============================================================

#pragma once

#include "types.h"
#include "vendor_config.h"
#include <optional>
#include <string>

namespace scan_master {

/**
 * Configuration for the detector.
 * The threshold is the minimum score needed to accept a match.
 * A threshold of 2 means at least two keyword hits (or one
 * high-weight hit) are needed.
 */
struct DetectorConfig {
    int threshold = 2;  // Minimum score to accept a vendor match
};

/**
 * Detect the vendor of a document.
 * Returns a DetectionResult with the winning vendor and score.
 *
 * The detection is purely based on the text content — it does
 * not modify the document or vendor registry.
 */
DetectionResult detect_vendor(const Document& doc,
                              const VendorRegistry& registry,
                              const DetectorConfig& config = {});

/**
 * Try to identify a vendor from context clues alone.
 * This is the fallback when keyword matching fails.
 * It checks phone numbers, domains, cities, and addresses
 * found in the text against the vendor registry's context data.
 *
 * Returns the vendor name and which clue matched, or empty optional
 * if no context match found.
 */
struct ContextMatch {
    std::string vendor_name;
    std::string clue_source;  // e.g. "phone:503-682-1777", "domain:autozonepro.com"
};

std::optional<ContextMatch> match_context(const Document& doc,
                                           const VendorRegistry& registry);

} // namespace scan_master
