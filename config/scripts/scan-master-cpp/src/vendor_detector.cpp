// ============================================================
//  vendor_detector.cpp — Vendor detection scoring engine
//
//  This replaces the ~300 lines of repetitive if/has/SCORE
//  blocks from the bash version with a data-driven approach.
//
//  The bash version had this pattern for each vendor:
//    SCORE=0
//    if has "keyword"; then SCORE+=3; fi
//    if has "other"; then SCORE+=2; fi
//    ...
//    if [ "$SCORE" -gt "$BEST_SCORE" ]; then ... fi
//
//  In C++, the keywords are data in VendorProfile, and the
//  scoring loop is generic. Adding a vendor requires zero
//  changes here — just add keywords to the vendor's JSON file.
// ============================================================

#include "vendor_detector.h"
#include "utils.h"

#include <algorithm>
#include <iostream>
#include <regex>
#include <string>

namespace scan_master {

// ── Pass 1: Keyword scoring ────────────────────────────────

/**
 * Score a single vendor's keywords against the document text.
 * Returns the total score and which clues matched.
 */
struct KeywordScore {
    int score = 0;
    std::vector<std::string> matched;
};

KeywordScore score_keywords(const VendorProfile& vendor, const std::string& text) {
    KeywordScore result;

    for (const auto& kw : vendor.keywords) {
        bool matched = false;

        if (kw.is_regex) {
            // Regex search — use std::regex
            try {
                std::regex re(kw.pattern, std::regex::ECMAScript | std::regex::icase);
                if (std::regex_search(text, re)) {
                    matched = true;
                }
            } catch (const std::regex_error& e) {
                // Invalid regex — skip this clue, log a warning
                std::cerr << "[Detector] Warning: invalid regex in vendor '"
                          << vendor.name << "': " << kw.pattern
                          << " (" << e.what() << ")" << std::endl;
            }
        } else {
            // Plain case-insensitive substring match
            matched = contains_ci(text, kw.pattern);
        }

        if (matched) {
            result.score += kw.weight;
            result.matched.push_back(kw.pattern);
        }
    }

    return result;
}

// ── Pass 2: Context clue matching ──────────────────────────

/**
 * Try to match a document against context clues.
 * This is the fallback for when keyword matching doesn't produce
 * a confident result.
 *
 * We extract structural signals from the text (phone numbers,
 * domains, addresses) and look them up in the vendor registry.
 */
std::optional<ContextMatch> match_context(const Document& doc,
                                           const VendorRegistry& registry) {
    // Combine both text versions for matching
    std::string combined_text = doc.text + "\n" + doc.text_no_layout;

    // Strategy 1: Phone number lookup
    // Extract all phone numbers in xxx-xxx-xxxx format
    auto phones = regex_find_all(combined_text, R"(\b\d{3}[-.]?\d{3}[-.]?\d{4}\b)");
    for (const auto& phone : phones) {
        // Normalize: ensure xxx-xxx-xxxx format for lookup
        std::string normalized;
        for (char c : phone) {
            if (c == '-' || c == '.') {
                if (normalized.size() == 3 || normalized.size() == 7) {
                    normalized += '-';
                }
            } else if (std::isdigit(c)) {
                normalized += c;
            }
        }
        // Ensure proper format
        if (normalized.size() == 10) {
            normalized = normalized.substr(0, 3) + "-" + normalized.substr(3, 3) + "-" + normalized.substr(6);
        }

        for (const auto& vendor : registry.all()) {
            auto it = vendor.context.phones.find(normalized);
            if (it != vendor.context.phones.end() && !it->second.empty()) {
                return ContextMatch{it->second, "phone:" + normalized};
            }
        }
    }

    // Strategy 2: Domain lookup
    auto domains = regex_find_all(combined_text, R"([\w][\w.-]*\.(com|net|org))");
    for (const auto& domain : domains) {
        // Case-insensitive lookup
        std::string lower_domain;
        for (char c : domain) {
            lower_domain += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        for (const auto& vendor : registry.all()) {
            for (const auto& [known_domain, vendor_name] : vendor.context.domains) {
                if (contains_ci(lower_domain, known_domain) || contains_ci(known_domain, lower_domain)) {
                    return ContextMatch{vendor_name, "domain:" + lower_domain};
                }
            }
        }
    }

    // Strategy 3: Address lookup
    // Addresses are trickier — they may have line breaks or OCR garble.
    // We do a case-insensitive substring match against known addresses.
    for (const auto& vendor : registry.all()) {
        for (const auto& [addr, vendor_name] : vendor.context.addresses) {
            if (!vendor_name.empty() && contains_ci(combined_text, addr)) {
                return ContextMatch{vendor_name, "address:" + addr};
            }
        }
    }

    // Strategy 3b: Structural heuristics
    // These catch document types that have distinctive patterns
    // but may not match any vendor's keywords or context clues.
    if (contains_ci(combined_text, "Transaction Amount") &&
        contains_ci(combined_text, "Auth Code")) {
        return ContextMatch{"CDKSimplePay", "struct:txn_receipt"};
    }
    if (contains_ci(combined_text, "Parts Invoice") &&
        (contains_ci(combined_text, "CDK Global") ||
         combined_text.find("Copyright") != std::string::npos)) {
        return ContextMatch{"DealerInvoice", "struct:cdk_parts"};
    }
    if (contains_ci(combined_text, "Transaction Type") &&
        contains_ci(combined_text, "Approved")) {
        return ContextMatch{"CDKSimplePay", "struct:cc_receipt"};
    }

    // Strategy 4: City + ZIP lookup
    // Look for known city|zip combos from the vendor registry
    for (const auto& vendor : registry.all()) {
        for (const auto& [city_key, vendor_name] : vendor.context.cities) {
            if (vendor_name.empty()) continue;
            // city_key format: "cityname|zipcode"
            auto pipe_pos = city_key.find('|');
            if (pipe_pos == std::string::npos) continue;

            std::string city = city_key.substr(0, pipe_pos);
            std::string zip = city_key.substr(pipe_pos + 1);

            // Check if both city and zip appear in the text
            if (contains_ci(combined_text, city) && contains_ci(combined_text, zip)) {
                return ContextMatch{vendor_name, "city:" + city_key};
            }
        }
    }

    return std::nullopt;
}

// ── Main detection entry point ─────────────────────────────

DetectionResult detect_vendor(const Document& doc,
                              const VendorRegistry& registry,
                              const DetectorConfig& config) {
    DetectionResult result;
    result.threshold = config.threshold;

    // Pass 1: Score all vendors by keyword matching
    int best_score = 0;
    std::string best_vendor;
    std::vector<std::string> best_clues;

    for (const auto& vendor : registry.all()) {
        auto kw_result = score_keywords(vendor, doc.text);

        // Also check the no-layout version if layout version didn't match
        if (kw_result.score == 0 && !doc.text_no_layout.empty()) {
            kw_result = score_keywords(vendor, doc.text_no_layout);
        }

        if (kw_result.score > best_score) {
            best_score = kw_result.score;
            best_vendor = vendor.name;
            best_clues = std::move(kw_result.matched);
        }
    }

    // If keyword matching is confident, return immediately
    if (best_score >= config.threshold) {
        result.vendor_name = best_vendor;
        result.score = best_score;
        result.matched_clues = std::move(best_clues);
        result.used_context_fallback = false;

        std::cout << "  Vendor: " << best_vendor
                  << " (score: " << best_score << ")" << std::endl;
        return result;
    }

    // Pass 2: Context clue fallback
    std::cout << "  Keyword matching weak (best score: " << best_score
              << "), trying context clues..." << std::endl;

    auto ctx_match = match_context(doc, registry);
    if (ctx_match.has_value()) {
        result.vendor_name = ctx_match->vendor_name;
        result.score = 1;  // Context matches get a nominal score
        result.matched_clues = {ctx_match->clue_source};
        result.used_context_fallback = true;

        std::cout << "  Vendor guessed from " << ctx_match->clue_source
                  << " (score was " << best_score << ")" << std::endl;
        return result;
    }

    // Nothing matched — UNKNOWN
    result.vendor_name = "UNKNOWN";
    result.score = 0;

    std::cout << "  Vendor: UNKNOWN (no match)" << std::endl;
    return result;
}

} // namespace scan_master
