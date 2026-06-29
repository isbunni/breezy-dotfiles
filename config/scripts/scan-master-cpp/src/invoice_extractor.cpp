// ============================================================
//  invoice_extractor.cpp — Invoice number extraction
//
//  This replaces the giant case statement from the bash version
//  (which had a separate extraction block for each vendor).
//
//  In the C++ version, extraction patterns are data in the
//  vendor's JSON config. This module just applies them.
//
//  For AutoZone, we have a special multi-strategy extractor
//  that handles the core-serial filtering and footer patterns.
//  This is the one vendor that needs custom logic — everything
//  else is pure regex.
// ============================================================

#include "invoice_extractor.h"
#include "utils.h"

#include <iostream>
#include <regex>

namespace scan_master {

// ── Generic regex-based extraction ──────────────────────────

/**
 * Try to extract an invoice number using the vendor's patterns.
 * Each pattern is tried in order. The first match that passes
 * validation wins.
 *
 * This covers the majority of vendors. AutoZone is special-cased
 * below because it needs multi-strategy fallback.
 */
InvoiceInfo extract_generic(const Document& doc, const VendorProfile& vendor) {
    InvoiceInfo info;
    info.vendor_name = vendor.name;

    // Combine both text versions for matching
    std::string combined = doc.text + "\n" + doc.text_no_layout;

    for (const auto& pattern : vendor.invoice_extraction.patterns) {
        // Try to extract a meaningful number from the regex match.
        // Strategy: find the first non-empty capture group that
        // looks like it could be an invoice number (contains digits).
        // Fall back to the full match if no suitable group found.
        //
        // This replaces the Perl \K syntax from the bash version.
        // \K resets the match start, effectively making everything
        // before it "invisible". We achieve the same by selecting
        // the right capture group.
        std::string number;
        try {
            std::regex re(pattern, std::regex::ECMAScript | std::regex::icase);
            std::smatch match;
            if (std::regex_search(combined, match, re)) {
                // Look for the first capture group containing digits
                for (size_t i = 1; i < match.size(); i++) {
                    if (match[i].matched && match[i].str().find_first_of("0123456789") != std::string::npos) {
                        number = match[i].str();
                        break;
                    }
                }
                // Fallback: full match if no group with digits found
                if (number.empty()) {
                    number = match.str();
                }
            }
        } catch (const std::regex_error& e) {
            std::cerr << "  Invalid regex: " << pattern << std::endl;
            continue;
        }
        if (!number.empty()) {

            // Validate if the vendor has a validation regex
            if (!vendor.invoice_extraction.validation_regex.empty()) {
                if (validate_invoice(vendor.name, number,
                                     vendor.invoice_extraction.validation_regex)) {
                    info.invoice_number = number;
                    info.valid = true;
                    return info;
                }
                // Invalid — keep trying other patterns
                std::cout << "  Invoice '" << number << "' failed validation, trying next pattern..." << std::endl;
            } else {
                // No validation regex — accept as-is
                info.invoice_number = number;
                info.valid = true;
                return info;
            }
        }
    }

    // Nothing matched
    return info;
}

// ── AutoZone special-case extraction ───────────────────────

/**
 * AutoZone has a complex invoice extraction problem:
//  1. Invoice numbers are 11 digits starting with 02227 or 02235
//  2. Core serial numbers look similar but are NOT invoices
//  3. OCR often garbles the "Invoice Number" label
//  4. Footer concatenation patterns need special handling
//
// This implements the same multi-strategy approach as the bash
// version, but in C++.
 */
InvoiceInfo extract_autozone(const Document& doc) {
    InvoiceInfo info;
    info.vendor_name = "AutoZone";

    std::string text = doc.text;

    // Strategy 1: "Invoice Number: 02227XXXXXXXX" (clean OCR)
    // Note: using capture group instead of \K (Perl-only)
    auto m = regex_first_match(text, R"(Invoice\s+Number\s*:\s*(0\d{10}))");
    if (m.has_value()) { info.invoice_number = *m; info.valid = true; return info; }

    // Strategy 2: "Invoice Number  02227XXXXXXXX" (OCR skips colon, stray 1)
    m = regex_first_match(text, R"(Invoice\s+Number\s+1?\s*(0\d{10}))");
    if (m.has_value()) { info.invoice_number = *m; info.valid = true; return info; }

    // Strategy 3: "Invoice Number" + dash/garble + digits
    m = regex_first_match(text, R"(Invoice\s+Number\s*[^0-9]*\s*1?\s*(0\d{10}))");
    if (m.has_value()) { info.invoice_number = *m; info.valid = true; return info; }

    // Strategy 4: Footer concatenation pattern
    // "<invoice_11><MMDDYY><letter>" e.g. "02227114715061226C"
    m = regex_first_match(text, R"(\b(02227\d{6}|02235\d{6})0[1-9]\d{4}[A-Z]\b)");
    if (m.has_value()) {
        info.invoice_number = m->substr(0, 11);
        info.valid = true;
        return info;
    }

    // Strategy 5: Find 02227/02235 numbers, exclude core serials
    auto candidates = regex_find_all(text, R"(\b02227\d{6}\b)");
    auto more = regex_find_all(text, R"(\b02235\d{6}\b)");
    candidates.insert(candidates.end(), more.begin(), more.end());

    // Extract core serials from the "Core Bank" section
    auto core_section = regex_first_match(text, R"(Core Bank[\s\S]*?Outstanding Cores)");
    if (!core_section.has_value()) {
        core_section = regex_first_match(text, R"(Cores Older[\s\S]*?Outstanding)");
    }
    std::vector<std::string> core_nums;
    if (core_section.has_value()) {
        core_nums = regex_find_all(*core_section, R"(\b0\d{10}\b)");
    }

    // Find first candidate that's NOT a core serial
    for (const auto& c : candidates) {
        bool is_core = false;
        for (const auto& cn : core_nums) {
            if (c == cn) { is_core = true; break; }
        }
        if (!is_core) {
            info.invoice_number = c;
            info.valid = true;
            return info;
        }
    }

    return info;  // Empty — nothing found
}

// ── Subtype detection ──────────────────────────────────────

std::string detect_subtype(const Document& doc, const VendorProfile& vendor) {
    // AutoZone has complex subtype logic
    if (vendor.name == "AutoZone") {
        bool is_return = contains_ci(doc.text, "Commercial Return");
        bool is_ar_credit = contains_ci(doc.text, "AR CREDIT") ||
                            contains_ci(doc.text, "AR CREDIT");
        bool is_labor = contains_ci(doc.text, "COMM.LABOR") ||
                        contains_ci(doc.text, "COMM. LABOR");

        if (is_return && is_labor) return "LaborClaim";
        if (is_return && is_ar_credit) return "ARCreditReturn";
        if (is_return) return "Return";
        if (is_ar_credit) return "ARCredit";
        if (is_labor) return "LaborClaim";
    }

    // NAPA: check for packing slip
    if (vendor.name == "NAPA") {
        if (contains_ci(doc.text, "Picking Ticket") ||
            contains_ci(doc.text, "packslip") ||
            contains_ci(doc.text, "pack slip")) {
            return "packslip";
        }
    }

    // Default: use the vendor's default subtype
    return vendor.default_subtype;
}

// ── Validation ─────────────────────────────────────────────

bool validate_invoice(const std::string& /*vendor_name*/,
                      const std::string& invoice_number,
                      const std::string& validation_regex) {
    if (validation_regex.empty()) return true;
    if (invoice_number.empty()) return false;

    try {
        std::regex re(validation_regex, std::regex::ECMAScript);
        return std::regex_match(invoice_number, re);
    } catch (const std::regex_error&) {
        // Invalid regex — accept the number anyway
        return true;
    }
}

// ── Main extraction entry point ────────────────────────────

InvoiceInfo extract_invoice(const Document& doc, const VendorProfile& vendor) {
    InvoiceInfo info;

    // AutoZone gets special multi-strategy extraction
    if (vendor.name == "AutoZone") {
        info = extract_autozone(doc);
    } else {
        info = extract_generic(doc, vendor);
    }

    // Detect subtype regardless of vendor
    info.subtype = detect_subtype(doc, vendor);

    if (!info.invoice_number.empty()) {
        std::cout << "  Invoice: " << info.invoice_number
                  << (info.valid ? " (valid)" : " (suspect)") << std::endl;
        if (!info.subtype.empty() && info.subtype != "Invoice") {
            std::cout << "  Subtype: " << info.subtype << std::endl;
        }
    } else {
        std::cout << "  ⚠ No invoice number extracted" << std::endl;
    }

    return info;
}

} // namespace scan_master
