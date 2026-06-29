// ============================================================
//  context_learner.h — Learn vendor clues from documents
//
//  This is the C++ equivalent of scan-learn.sh. It extracts
//  phone numbers, domains, and addresses from a document and
//  associates them with a vendor. The learned clues are saved
//  back to the vendor's JSON file so future scans can use them.
//
//  The learning is additive — it never removes existing clues,
//  only adds new ones. If a clue already exists but maps to a
//  different vendor, it's flagged as a conflict rather than
//  silently overwritten.
// ============================================================

#pragma once

#include "types.h"
#include "pipeline.h"
#include "vendor_config.h"
#include <filesystem>
#include <string>
#include <vector>

namespace scan_master {

/**
 * A single clue learned from a document.
 */
struct LearnedClue {
    std::string type;       // "phone", "domain", "address"
    std::string value;      // The actual phone/domain/address
    std::string vendor;     // Which vendor it maps to
};

/**
 * Extract learnable clues from a document.
 * Returns phone numbers, domains, and addresses found in the text.
 *
 * This doesn't save anything — it just identifies candidates.
 * The caller decides what to do with them.
 */
struct ClueCandidates {
    std::vector<std::string> phones;
    std::vector<std::string> domains;
    std::vector<std::string> addresses;
};

ClueCandidates extract_clues(const Document& doc);

/**
 * Learn from a document: extract clues, associate them with a vendor,
 * and save to the vendor's JSON file.
 *
 * @param doc       The document to learn from
 * @param vendor    The vendor to associate clues with
 * @param registry  The vendor registry (to save updates)
 * @param vendor_dir  Directory containing vendor JSON files
 * @return          Number of new clues learned
 *
 * Existing clues that conflict with the new vendor are NOT
 * overwritten. They're reported as warnings instead.
 */
int learn_from_document(const Document& doc,
                        const std::string& vendor_name,
                        VendorRegistry& registry,
                        const std::filesystem::path& vendor_dir);

/**
 * Learn from a PDF file (extracts text first).
 * Convenience wrapper for the CLI tool.
 */
int learn_from_pdf(const std::filesystem::path& pdf_path,
                   const std::string& vendor_name,
                   VendorRegistry& registry,
                   const std::filesystem::path& vendor_dir);

} // namespace scan_master
