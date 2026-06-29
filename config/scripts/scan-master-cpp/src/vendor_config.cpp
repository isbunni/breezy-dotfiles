// ============================================================
//  vendor_config.cpp — Implementation of vendor profile loading
//
//  JSON parsing using nlohmann/json. The schema matches the
//  vendor JSON files in the vendors/ directory.
//
//  If you're adding a new field to VendorProfile, you need to
//  update three places:
//    1. types.h — the struct definition
//    2. This file — parse_profile() and serialize_profile()
//    3. The vendor JSON files
// ============================================================

#include "vendor_config.h"
#include "utils.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace scan_master {
using json = nlohmann::json;

// ── JSON parsing ───────────────────────────────────────────

/**
 * Parse a single JSON object into a VendorProfile.
 * Throws std::runtime_error on invalid JSON structure.
 *
 * Expected JSON format (see vendors/autozone.json for example):
 * {
 *   "name": "AutoZone",
 *   "keywords": [
 *     { "pattern": "autozone", "weight": 3, "is_regex": false },
 *     { "pattern": "0222[0-9]", "weight": 2, "is_regex": true }
 *   ],
 *   "context": {
 *     "phones": { "503-371-1648": "AutoZone" },
 *     "domains": { "autozonepro.com": "AutoZone" },
 *     "cities": { "wilsonville|97070": "TEC" },
 *     "addresses": { "7950 SW Burns Way": "TEC" }
 *   },
 *   "invoice_extraction": {
 *     "patterns": ["Invoice\\s+Number\\s*:\\s*\\K0\\d{10}"],
 *     "validation_regex": "^022[0-9]{8}$"
 *   },
 *   "file_naming": {
 *     "normal_template": "{vendor}_{invoice}.pdf",
 *     "unknown_template": "{vendor}_UNKNOWN_{date}_{time}.pdf"
 *   },
 *   "default_subtype": "Invoice"
 * }
 */
VendorProfile VendorRegistry::parse_profile(const json& j) {
    VendorProfile profile;

    // Required fields
    if (!j.contains("name") || !j["name"].is_string()) {
        throw std::runtime_error("Vendor profile missing 'name' field");
    }
    profile.name = j["name"].get<std::string>();

    // Keywords (optional — some vendors may rely only on context)
    if (j.contains("keywords") && j["keywords"].is_array()) {
        for (const auto& kw : j["keywords"]) {
            KeywordClue clue;
            clue.pattern = kw.value("pattern", "");
            clue.weight = kw.value("weight", 1);
            clue.is_regex = kw.value("is_regex", false);
            if (!clue.pattern.empty()) {
                profile.keywords.push_back(std::move(clue));
            }
        }
    }

    // Context clues (optional)
    if (j.contains("context") && j["context"].is_object()) {
        const auto& ctx = j["context"];

        if (ctx.contains("phones") && ctx["phones"].is_object()) {
            for (auto& [key, val] : ctx["phones"].items()) {
                profile.context.phones[key] = val.get<std::string>();
            }
        }
        if (ctx.contains("domains") && ctx["domains"].is_object()) {
            for (auto& [key, val] : ctx["domains"].items()) {
                profile.context.domains[key] = val.get<std::string>();
            }
        }
        if (ctx.contains("cities") && ctx["cities"].is_object()) {
            for (auto& [key, val] : ctx["cities"].items()) {
                profile.context.cities[key] = val.get<std::string>();
            }
        }
        if (ctx.contains("addresses") && ctx["addresses"].is_object()) {
            for (auto& [key, val] : ctx["addresses"].items()) {
                profile.context.addresses[key] = val.get<std::string>();
            }
        }
    }

    // Invoice extraction (optional)
    if (j.contains("invoice_extraction") && j["invoice_extraction"].is_object()) {
        const auto& inv = j["invoice_extraction"];
        if (inv.contains("patterns") && inv["patterns"].is_array()) {
            for (const auto& p : inv["patterns"]) {
                if (p.is_string()) {
                    profile.invoice_extraction.patterns.push_back(p.get<std::string>());
                }
            }
        }
        if (inv.contains("validation_regex") && inv["validation_regex"].is_string()) {
            profile.invoice_extraction.validation_regex = inv["validation_regex"].get<std::string>();
        }
    }

    // File naming (optional — has sensible defaults)
    if (j.contains("file_naming") && j["file_naming"].is_object()) {
        const auto& fn = j["file_naming"];
        profile.file_naming.normal_template = fn.value("normal_template",
            "{vendor}_{invoice}.pdf");
        profile.file_naming.unknown_template = fn.value("unknown_template",
            "{vendor}_UNKNOWN_{date}_{time}.pdf");
    } else {
        profile.file_naming.normal_template = "{vendor}_{invoice}.pdf";
        profile.file_naming.unknown_template = "{vendor}_UNKNOWN_{date}_{time}.pdf";
    }

    // Default subtype
    profile.default_subtype = j.value("default_subtype", "Invoice");

    return profile;
}

/**
 * Serialize a VendorProfile back to JSON.
 * Used by the context learner to save learned clues.
 */
json VendorRegistry::serialize_profile(const VendorProfile& vendor) {
    json j;
    j["name"] = vendor.name;

    // Keywords
    j["keywords"] = json::array();
    for (const auto& kw : vendor.keywords) {
        json kw_json;
        kw_json["pattern"] = kw.pattern;
        kw_json["weight"] = kw.weight;
        kw_json["is_regex"] = kw.is_regex;
        j["keywords"].push_back(kw_json);
    }

    // Context
    json ctx;
    if (!vendor.context.phones.empty()) ctx["phones"] = vendor.context.phones;
    if (!vendor.context.domains.empty()) ctx["domains"] = vendor.context.domains;
    if (!vendor.context.cities.empty()) ctx["cities"] = vendor.context.cities;
    if (!vendor.context.addresses.empty()) ctx["addresses"] = vendor.context.addresses;
    if (!ctx.is_null()) j["context"] = ctx;

    // Invoice extraction
    json inv;
    if (!vendor.invoice_extraction.patterns.empty()) inv["patterns"] = vendor.invoice_extraction.patterns;
    if (!vendor.invoice_extraction.validation_regex.empty()) inv["validation_regex"] = vendor.invoice_extraction.validation_regex;
    if (!inv.is_null()) j["invoice_extraction"] = inv;

    // File naming
    json fn;
    fn["normal_template"] = vendor.file_naming.normal_template;
    fn["unknown_template"] = vendor.file_naming.unknown_template;
    j["file_naming"] = fn;

    j["default_subtype"] = vendor.default_subtype;

    return j;
}

// ── Public interface ───────────────────────────────────────

void VendorRegistry::load_directory(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir)) {
        std::cerr << "[VendorRegistry] Warning: directory does not exist: "
                  << dir << std::endl;
        return;
    }

    // Iterate all .json files in the directory, sorted for determinism
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        load_file(file);
    }

    std::cout << "[VendorRegistry] Loaded " << vendors_.size()
              << " vendor profile(s) from " << dir << std::endl;
}

void VendorRegistry::load_file(const std::filesystem::path& file) {
    try {
        std::ifstream f(file);
        if (!f.is_open()) {
            std::cerr << "[VendorRegistry] Warning: cannot open " << file << std::endl;
            return;
        }
        json j = json::parse(f);
        vendors_.push_back(parse_profile(j));
    } catch (const std::exception& e) {
        std::cerr << "[VendorRegistry] Warning: failed to parse " << file
                  << ": " << e.what() << std::endl;
    }
}

const VendorProfile* VendorRegistry::find(const std::string& name) const {
    for (const auto& v : vendors_) {
        if (v.name == name) {
            return &v;
        }
    }
    return nullptr;
}

void VendorRegistry::save_vendor(const VendorProfile& vendor,
                                  const std::filesystem::path& dir) const {
    ensure_dir(dir);
    auto file = dir / (vendor.name + ".json");
    auto j = serialize_profile(vendor);

    std::ofstream f(file);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot write vendor file: " + file.string());
    }
    f << j.dump(2) << std::endl;  // 2-space indent for readability
}

} // namespace scan_master
