// ============================================================
//  context_learner.cpp — Learn vendor clues from documents
//
//  This replaces scan-learn.sh. It extracts phone numbers,
//  domains, and addresses from a document's text and saves
//  them as context clues in the vendor's JSON file.
//
//  The learning is additive and conflict-aware:
//    - New clues are added to the vendor's context
//    - Existing clues that map to a different vendor are
//      flagged as warnings (not overwritten)
//    - Empty mappings (null values in JSON) are skipped
// ============================================================

#include "context_learner.h"
#include "vendor_config.h"
#include "utils.h"

#include <algorithm>
#include <iostream>
#include <regex>

namespace scan_master {

// ── Extract candidate clues from text ──────────────────────

ClueCandidates extract_clues(const Document& doc) {
    ClueCandidates candidates;
    std::string combined = doc.text + "\n" + doc.text_no_layout;

    // Extract phone numbers (xxx-xxx-xxxx, xxx.xxx.xxxx, xxxxxxxxxx)
    auto phone_matches = regex_find_all(combined, R"(\b\d{3}[-.]?\d{3}[-.]?\d{4}\b)");
    for (const auto& p : phone_matches) {
        // Normalize to xxx-xxx-xxxx
        std::string digits;
        for (char c : p) {
            if (std::isdigit(c)) digits += c;
        }
        if (digits.size() == 10) {
            std::string normalized = digits.substr(0,3) + "-" + digits.substr(3,3) + "-" + digits.substr(6);
            // Deduplicate
            if (std::find(candidates.phones.begin(), candidates.phones.end(), normalized)
                == candidates.phones.end()) {
                candidates.phones.push_back(std::move(normalized));
            }
        }
    }

    // Extract domains
    auto domain_matches = regex_find_all(combined, R"([\w][\w.-]*\.(com|net|org))");
    for (const auto& d : domain_matches) {
        // Skip generic personal email domains
        std::string lower;
        for (char c : d) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("gmail.com") != std::string::npos ||
            lower.find("yahoo.com") != std::string::npos ||
            lower.find("hotmail.com") != std::string::npos ||
            lower.find("outlook.com") != std::string::npos) {
            continue;
        }
        if (std::find(candidates.domains.begin(), candidates.domains.end(), lower)
            == candidates.domains.end()) {
            candidates.domains.push_back(std::move(lower));
        }
    }

    // Extract street addresses (number + street name pattern)
    // This is a simple heuristic: digits followed by a street type
    auto addr_matches = regex_find_all(combined,
        R"(\d{3,4}\s+(?:SW|SE|NW|NE|N|S|E|W)?\s*\w+\s+(?:Street|St|Way|Ave|Road|Rd|Boulevard|Blvd|Drive|Dr|Lane|Ln|Court|Ct))");
    for (const auto& a : addr_matches) {
        // Normalize whitespace
        std::string normalized = a;
        // Collapse multiple spaces to one
        auto end = std::unique(normalized.begin(), normalized.end(),
            [](char a, char b) { return a == ' ' && b == ' '; });
        normalized.erase(end, normalized.end());

        if (std::find(candidates.addresses.begin(), candidates.addresses.end(), normalized)
            == candidates.addresses.end()) {
            candidates.addresses.push_back(std::move(normalized));
        }
    }

    return candidates;
}

// ── Learn from a document ──────────────────────────────────

int learn_from_document(const Document& doc,
                        const std::string& vendor_name,
                        VendorRegistry& registry,
                        const std::filesystem::path& vendor_dir) {
    // Find the vendor in the registry
    const auto* vendor = registry.find(vendor_name);
    if (!vendor) {
        std::cerr << "[Learner] Vendor '" << vendor_name << "' not found in registry." << std::endl;
        return -1;
    }

    // Make a mutable copy so we can add clues
    VendorProfile updated = *vendor;
    int learned = 0;

    // Extract clues from the document
    auto clues = extract_clues(doc);

    // Learn phone numbers
    for (const auto& phone : clues.phones) {
        auto it = updated.context.phones.find(phone);
        if (it == updated.context.phones.end()) {
            // New phone — add it
            updated.context.phones[phone] = vendor_name;
            std::cout << "  📱 Learned phone: " << phone << " → " << vendor_name << std::endl;
            learned++;
        } else if (it->second.empty()) {
            // Was null — fill it in
            it->second = vendor_name;
            std::cout << "  📱 Updated phone: " << phone << " → " << vendor_name << std::endl;
            learned++;
        } else if (it->second != vendor_name) {
            // Conflict — already mapped to a different vendor
            std::cout << "  ⚠ Phone " << phone << " already mapped to "
                      << it->second << " (not " << vendor_name << ")" << std::endl;
        }
    }

    // Learn domains
    for (const auto& domain : clues.domains) {
        auto it = updated.context.domains.find(domain);
        if (it == updated.context.domains.end()) {
            updated.context.domains[domain] = vendor_name;
            std::cout << "  🌐 Learned domain: " << domain << " → " << vendor_name << std::endl;
            learned++;
        } else if (it->second.empty()) {
            it->second = vendor_name;
            std::cout << "  🌐 Updated domain: " << domain << " → " << vendor_name << std::endl;
            learned++;
        } else if (it->second != vendor_name) {
            std::cout << "  ⚠ Domain " << domain << " already mapped to "
                      << it->second << " (not " << vendor_name << ")" << std::endl;
        }
    }

    // Learn addresses
    for (const auto& addr : clues.addresses) {
        auto it = updated.context.addresses.find(addr);
        if (it == updated.context.addresses.end()) {
            updated.context.addresses[addr] = vendor_name;
            std::cout << "  🏠 Learned address: " << addr << " → " << vendor_name << std::endl;
            learned++;
        } else if (it->second.empty()) {
            it->second = vendor_name;
            std::cout << "  🏠 Updated address: " << addr << " → " << vendor_name << std::endl;
            learned++;
        } else if (it->second != vendor_name) {
            std::cout << "  ⚠ Address " << addr << " already mapped to "
                      << it->second << " (not " << vendor_name << ")" << std::endl;
        }
    }

    // Save the updated vendor profile
    if (learned > 0) {
        registry.save_vendor(updated, vendor_dir);
        std::cout << "✅ Learned " << learned << " new clue(s) for " << vendor_name << "." << std::endl;
    } else {
        std::cout << "ℹ No new clues found — everything was already known." << std::endl;
    }

    return learned;
}

// ── Learn from a PDF file ──────────────────────────────────

int learn_from_pdf(const std::filesystem::path& pdf_path,
                   const std::string& vendor_name,
                   VendorRegistry& registry,
                   const std::filesystem::path& vendor_dir) {
    if (!std::filesystem::exists(pdf_path)) {
        std::cerr << "[Learner] File not found: " << pdf_path << std::endl;
        return -1;
    }

    // Extract text from the PDF
    auto text = extract_text(pdf_path);

    Document doc;
    doc.pdf_path = pdf_path.string();
    doc.text = std::move(text.layout);
    doc.text_no_layout = std::move(text.no_layout);

    if (doc.text.empty() && doc.text_no_layout.empty()) {
        std::cerr << "[Learner] No text could be extracted from " << pdf_path << std::endl;
        return -1;
    }

    std::cout << "[Learner] Learning from: " << pdf_path.filename().string() << std::endl;
    std::cout << "   Vendor: " << vendor_name << std::endl;
    std::cout << std::endl;

    return learn_from_document(doc, vendor_name, registry, vendor_dir);
}

} // namespace scan_master
