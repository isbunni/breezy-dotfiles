// ============================================================
//  utils.cpp — Implementation of utility functions
//
//  These replace the shell helpers (has(), grep -oP, mktemp -d, etc.)
//  with proper C++ equivalents that actually report errors.
// ============================================================

#include "utils.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

namespace scan_master {

// ── External command execution ──────────────────────────────

ExecResult exec_command(const std::string& cmd, int timeout_seconds) {
    // We use a temp file approach for stdout/stderr capture.
    // This is portable and avoids the complexity of posix_spawn.
    // For each invocation we create temp files, redirect the command
    // output to them, then read them back.

    // Temp files for stdout and stderr
    char tmp_out[] = "/tmp/scan_master_out_XXXXXX";
    char tmp_err[] = "/tmp/scan_master_err_XXXXXX";

    int fd_out = mkstemp(tmp_out);
    int fd_err = mkstemp(tmp_err);

    if (fd_out < 0 || fd_err < 0) {
        // Clean up whichever ones succeeded
        if (fd_out >= 0) { close(fd_out); unlink(tmp_out); }
        if (fd_err >= 0) { close(fd_err); unlink(tmp_err); }
        throw std::runtime_error("Failed to create temp files for command execution");
    }
    close(fd_out);
    close(fd_err);

    // Build the shell command with redirection
    // We redirect both stdout and stderr to our temp files.
    std::string full_cmd = cmd + " >" + tmp_out + " 2>" + tmp_err;

    // Fork and exec
    pid_t pid = fork();
    if (pid < 0) {
        unlink(tmp_out);
        unlink(tmp_err);
        throw std::runtime_error("fork() failed");
    }

    if (pid == 0) {
        // Child process
        execl("/bin/sh", "sh", "-c", full_cmd.c_str(), nullptr);
        _exit(127); // execl failed
    }

    // Parent process: wait for child with timeout
    int status;
    int waited;
    if (timeout_seconds > 0) {
        // Simple polling loop for timeout
        // Not perfect (doesn't account for signal interruptions) but
        // good enough for our use case.
        int elapsed = 0;
        while (elapsed < timeout_seconds) {
            waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) break;  // Child exited
            if (waited < 0) break;     // Error
            sleep(1);
            elapsed++;
        }
        if (waited != pid) {
            // Timeout — kill the child
            kill(pid, SIGTERM);
            sleep(1);
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    } else {
        waited = waitpid(pid, &status, 0);
    }

    // Read captured output
    ExecResult result;
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    // Read stdout
    {
        std::ifstream f(tmp_out);
        std::stringstream ss;
        ss << f.rdbuf();
        result.stdout = ss.str();
    }
    // Read stderr
    {
        std::ifstream f(tmp_err);
        std::stringstream ss;
        ss << f.rdbuf();
        result.stderr = ss.str();
    }

    // Clean up temp files
    unlink(tmp_out);
    unlink(tmp_err);

    return result;
}

ExecResult exec_command_checked(const std::string& cmd, int timeout_seconds) {
    auto result = exec_command(cmd, timeout_seconds);
    if (!result.ok()) {
        std::ostringstream oss;
        oss << "Command failed (exit " << result.exit_code << "): " << cmd;
        if (!result.stderr.empty()) {
            oss << "\nstderr: " << result.stderr;
        }
        throw std::runtime_error(oss.str());
    }
    return result;
}

// ── TempDir (RAII) ─────────────────────────────────────────

TempDir::TempDir() {
    // mkdtemp requires a mutable template ending in XXXXXX
    char template_path[] = "/tmp/scan_master_XXXXXX";
    char* result = mkdtemp(template_path);
    if (!result) {
        throw std::runtime_error("mkdtemp() failed");
    }
    path_ = result;
}

TempDir::~TempDir() {
    // Recursively delete the temp directory
    // No-op if it was moved away
    if (!path_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        // We intentionally ignore errors in the destructor.
        // If cleanup fails, there's nothing we can do about it.
    }
}

TempDir::TempDir(TempDir&& other) noexcept : path_(std::move(other.path_)) {
    other.path_.clear();
}

TempDir& TempDir::operator=(TempDir&& other) noexcept {
    if (this != &other) {
        // Clean up our current dir first
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
        path_ = std::move(other.path_);
        other.path_.clear();
    }
    return *this;
}

// ── Timestamps ─────────────────────────────────────────────

std::string timestamp_now() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string date_now() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d");
    return oss.str();
}

// ── String helpers ─────────────────────────────────────────

bool contains_ci(const std::string& haystack, const std::string& needle) {
    // Case-insensitive substring search.
    // We lowercase both strings and do a simple find.
    // This is faster than regex for plain text matching.

    if (needle.empty()) return true;
    if (haystack.empty()) return false;

    // Convert both to lowercase
    std::string lower_hay;
    lower_hay.reserve(haystack.size());
    for (char c : haystack) {
        lower_hay += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    std::string lower_needle;
    lower_needle.reserve(needle.size());
    for (char c : needle) {
        lower_needle += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return lower_hay.find(lower_needle) != std::string::npos;
}

std::vector<std::string> regex_find_all(const std::string& text, const std::string& pattern) {
    std::vector<std::string> matches;
    try {
        std::regex re(pattern, std::regex::ECMAScript);
        auto begin = std::sregex_iterator(text.begin(), text.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            matches.push_back(it->str());
        }
    } catch (const std::regex_error& e) {
        // Invalid regex — return empty rather than crash.
        // The caller should validate patterns at load time, not runtime.
    }
    return matches;
}

std::optional<std::string> regex_first_match(const std::string& text, const std::string& pattern) {
    try {
        std::regex re(pattern, std::regex::ECMAScript);
        std::smatch match;
        if (std::regex_search(text, match, re)) {
            // If there are capture groups, prefer group 1.
            // This replaces the Perl \K behavior from the bash scripts.
            // e.g. "Invoice Number: (0\d{10})" returns the digits, not the full match.
            if (match.size() > 1 && match[1].matched) {
                return match[1].str();
            }
            return match.str();
        }
    } catch (const std::regex_error& e) {
        // Invalid regex — return nullopt
    }
    return std::nullopt;
}

// ── Filesystem helpers ─────────────────────────────────────

void ensure_dir(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        throw std::runtime_error("Failed to create directory " + dir.string() + ": " + ec.message());
    }
}

void copy_file(const std::string& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::copy_file(std::filesystem::path(from), to,
        std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        throw std::runtime_error("Failed to copy " + from + " → " + to.string()
            + ": " + ec.message());
    }
}

std::filesystem::path make_unique_path(std::filesystem::path target) {
    // If the file doesn't exist, return as-is
    if (!std::filesystem::exists(target)) {
        return target;
    }

    // Otherwise, append _1, _2, etc. until we find a free name
    std::string stem = target.stem().string();
    std::string ext = target.extension().string();
    std::filesystem::path parent = target.parent_path();

    int counter = 1;
    while (true) {
        std::filesystem::path candidate = parent / (stem + "_" + std::to_string(counter) + ext);
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
        counter++;
        // Safety valve — don't loop forever
        if (counter > 10000) {
            throw std::runtime_error("Could not find unique filename for " + target.string());
        }
    }
}

} // namespace scan_master
