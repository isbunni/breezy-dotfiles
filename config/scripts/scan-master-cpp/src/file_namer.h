// ============================================================
//  file_namer.h — Generates output filenames from scan results
//
//  Replaces the bash version's inline filename construction
//  (which was a mix of hardcoded strings and case statements).
//
//  Templates come from the vendor's JSON config:
//    Normal:   "{vendor}_{invoice}.pdf"
//    Unknown:  "{vendor}_UNKNOWN_{date}_{time}.pdf"
//
//  Available placeholders:
//    {vendor}  — vendor name
//    {invoice} — extracted invoice number
//    {subtype} — document subtype
//    {date}    — YYYYMMDD
//    {time}    — HHMMSS
// ============================================================

#pragma once

#include "types.h"
#include <filesystem>

namespace scan_master {

/**
 * Generate the output filename for a scanned document.
 *
 * @param vendor    The matched vendor profile
 * @param invoice   The extracted invoice info
 * @param save_dir  Directory to save into
 * @return          A unique file path (won't overwrite existing files)
 */
std::filesystem::path generate_filename(const VendorProfile& vendor,
                                        const InvoiceInfo& invoice,
                                        const std::filesystem::path& save_dir);

/**
 * Generate a filename for an UNKNOWN document.
 * Uses the unknown template with a timestamp.
 */
std::filesystem::path generate_unknown_filename(const std::filesystem::path& save_dir);

} // namespace scan_master
