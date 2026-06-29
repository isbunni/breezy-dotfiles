// ============================================================
//  invoice_extractor.h — Per-vendor invoice number extraction
//
//  Each vendor has its own regex patterns for extracting
//  invoice numbers from OCR text. This module tries each
//  pattern in order and returns the first match that passes
//  validation.
//
//  The extraction is data-driven: the patterns come from
//  the vendor's JSON config, not hardcoded logic.
//
//  For vendors with complex extraction (like AutoZone with
//  its multiple strategies and core-serial filtering), we
//  also support "smart extraction" — a function that does
//  more than just regex matching.
// ============================================================

#pragma once

#include "types.h"
#include <optional>
#include <string>

namespace scan_master {

/**
 * Extract invoice information for a detected vendor.
 *
 * @param doc       The document to extract from
 * @param vendor    The vendor profile (has extraction patterns)
 * @return          InvoiceInfo with extracted number, or empty if nothing found
 *
 * This tries each pattern in the vendor's invoice_extraction.patterns
 * list in order. The first match that passes the vendor's
 * validation_regex wins.
 */
InvoiceInfo extract_invoice(const Document& doc, const VendorProfile& vendor);

/**
 * Try to detect a subtype from the document text.
 * e.g. "Commercial Return" → "Return", "AR CREDIT" → "ARCredit"
 *
 * This is vendor-specific logic. For most vendors, we just
 * check for known keywords. For AutoZone, it's more complex
 * (return + labor claim combinations).
 */
std::string detect_subtype(const Document& doc, const VendorProfile& vendor);

/**
 * Validate an extracted invoice number against the vendor's
 * validation regex. Returns true if valid.
 */
bool validate_invoice(const std::string& vendor_name,
                      const std::string& invoice_number,
                      const std::string& validation_regex);

} // namespace scan_master
