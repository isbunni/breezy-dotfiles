// ============================================================
//  utils.h — Shared utility functions
//
//  These are the things that would be shell one-liners in the
//  bash version but need proper error handling in C++.
//  All functions are free functions in the scan_master namespace.
// ============================================================

#pragma once

#include "types.h"
#include <filesystem>

namespace scan_master {

/**
 * Execute a command and capture its output.
 * This is how we call external tools (scanimage, ocrmypdf, gs, pdftotext).
 *
 * We don't use popen() because it only captures stdout and doesn't
 * give us the exit code cleanly. Instead we use posix_spawn or
 * a simple fork/exec with pipe capture.
 *
 * For B: if you're reading this wondering why not just use system() —
 * system() discards exit codes and output. We need both.
 */
ExecResult exec_command(const std::string& cmd, int timeout_seconds = 120);

/**
 * Execute a command, throwing on non-zero exit.
 * Use this when failure is not an option (e.g. scanimage).
 */
ExecResult exec_command_checked(const std::string& cmd, int timeout_seconds = 120);

/**
 * Create a temporary directory that cleans itself up on destruction.
 * Uses mkdtemp under the hood. The path is guaranteed to exist
 * and be unique.
 *
 * RAII: the directory is deleted when this object goes out of scope.
 * No manual cleanup, no trap 'rm -rf' needed.
 */
class TempDir {
public:
    TempDir();
    ~TempDir();

    // Non-copyable, movable
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&& other) noexcept;
    TempDir& operator=(TempDir&& other) noexcept;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

/**
 * Get a timestamp string suitable for filenames.
 * Format: YYYYMMDD_HHMMSS
 */
std::string timestamp_now();

/**
 * Get a compact date string.
 * Format: YYYYMMDD
 */
std::string date_now();

/**
 * Case-insensitive substring search.
 * Returns true if `needle` appears in `haystack` (case-insensitive).
 */
bool contains_ci(const std::string& haystack, const std::string& needle);

/**
 * Extract all regex matches from a string.
 * Returns a vector of match strings (the full match, not groups).
 */
std::vector<std::string> regex_find_all(const std::string& text, const std::string& pattern);

/**
 * Extract the first regex match from a string.
 * Returns std::nullopt if no match found.
 * If the regex has capture groups, returns group 1 (not the full match).
 * This replaces the Perl \K behavior used in the bash scripts.
 */
std::optional<std::string> regex_first_match(const std::string& text, const std::string& pattern);

/**
 * Ensure a directory exists, creating it if necessary.
 * Like mkdir -p.
 */
void ensure_dir(const std::filesystem::path& dir);

/**
 * Copy a file, overwriting the destination if it exists.
 * Throws on failure.
 */
void copy_file(const std::string& from, const std::filesystem::path& to);

/**
 * Generate a unique filename by appending a counter if the file already exists.
 * e.g. if "AutoZone_123.pdf" exists, returns "AutoZone_123_1.pdf"
 */
std::filesystem::path make_unique_path(std::filesystem::path target);

} // namespace scan_master
