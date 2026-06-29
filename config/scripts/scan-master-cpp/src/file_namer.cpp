// ============================================================
//  file_namer.cpp — Filename generation
//
//  Simple template substitution. No magic, just string replacement.
// ============================================================

#include "file_namer.h"
#include "utils.h"

#include <filesystem>

namespace scan_master {

/**
 * Replace all occurrences of a placeholder in a string.
 */
static std::string replace_all(std::string str,
                                const std::string& from,
                                const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.size(), to);
        pos += to.size();
    }
    return str;
}

std::filesystem::path generate_filename(const VendorProfile& vendor,
                                        const InvoiceInfo& invoice,
                                        const std::filesystem::path& save_dir) {
    std::string tpl = vendor.file_naming.normal_template;

    // If no invoice number, fall back to unknown template
    if (invoice.invoice_number.empty()) {
        tpl = vendor.file_naming.unknown_template;
    }

    // Substitute placeholders
    tpl = replace_all(tpl, "{vendor}", vendor.name);
    tpl = replace_all(tpl, "{invoice}", invoice.invoice_number);
    tpl = replace_all(tpl, "{subtype}", invoice.subtype);
    tpl = replace_all(tpl, "{date}", date_now());
    tpl = replace_all(tpl, "{time}", timestamp_now().substr(9));  // HHMMSS part

    // If subtype is meaningful, include it in the filename
    if (!invoice.subtype.empty() && invoice.subtype != "Invoice" &&
        invoice.invoice_number.find(invoice.subtype) == std::string::npos) {
        // Insert subtype before .pdf
        auto pdf_pos = tpl.rfind(".pdf");
        if (pdf_pos != std::string::npos) {
            tpl.insert(pdf_pos, "_" + invoice.subtype);
        }
    }

    auto path = save_dir / tpl;
    return make_unique_path(path);
}

std::filesystem::path generate_unknown_filename(const std::filesystem::path& save_dir) {
    std::string tpl = "UNKNOWN_{date}_{time}.pdf";
    tpl = replace_all(tpl, "{date}", date_now());
    tpl = replace_all(tpl, "{time}", timestamp_now().substr(9));

    auto path = save_dir / tpl;
    return make_unique_path(path);
}

} // namespace scan_master
