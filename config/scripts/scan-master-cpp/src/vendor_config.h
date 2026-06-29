// ============================================================
//  vendor_config.h — Loads vendor profiles from JSON files
//
//  Each vendor has its own JSON file in the vendors/ directory.
//  This module reads them and builds VendorProfile structs.
//
//  Why separate files instead of one big JSON?
//  - Each vendor is independent. Adding one doesn't risk breaking
//    another's config.
//  - Easier to review: "what's NAPA's config?" → open napa.json
//  - The learning system can update individual vendor files without
//    rewriting the whole database.
//
//  The vendor files live in the source tree under vendors/ but
//  could also be loaded from ~/.config/scan-master/vendors/ for
//  user customizations.
// ============================================================

#pragma once

#include "types.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <vector>

namespace scan_master {

/**
 * Manages the collection of vendor profiles.
 *
 * Usage:
 *   VendorRegistry registry;
 *   registry.load_directory("vendors/");
 *
 *   // Look up a specific vendor
 *   auto* az = registry.find("AutoZone");
 *
 *   // Get all vendors (for scoring)
 *   const auto& all = registry.all();
 *
 * This class owns the loaded profiles. It's not a singleton —
 * create one per scan session. This makes it testable and
 * avoids global state.
 */
class VendorRegistry {
public:
    VendorRegistry() = default;

    /**
     * Load all vendor profiles from a directory.
     * Each .json file in the directory becomes one VendorProfile.
     * Files that fail to parse are skipped with a warning.
     */
    void load_directory(const std::filesystem::path& dir);

    /**
     * Load a single vendor profile from a specific file.
     */
    void load_file(const std::filesystem::path& file);

    /**
     * Find a vendor by name. Returns nullptr if not found.
     */
    const VendorProfile* find(const std::string& name) const;

    /**
     * Get all loaded vendor profiles.
     */
    const std::vector<VendorProfile>& all() const { return vendors_; }

    /**
     * Get the number of loaded vendors.
     */
    size_t size() const { return vendors_.size(); }

    /**
     * Save a vendor profile back to its JSON file.
     * Used by the context learner to persist new clues.
     */
    void save_vendor(const VendorProfile& vendor, const std::filesystem::path& dir) const;

private:
    std::vector<VendorProfile> vendors_;

    // Helper: parse a single JSON object into a VendorProfile
    static VendorProfile parse_profile(const nlohmann::json& j);

    // Helper: serialize a VendorProfile to JSON
    static nlohmann::json serialize_profile(const VendorProfile& vendor);
};

} // namespace scan_master
