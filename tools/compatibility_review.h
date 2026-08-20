#ifndef KEELS2_TOOLS_COMPATIBILITY_REVIEW_H
#define KEELS2_TOOLS_COMPATIBILITY_REVIEW_H

#include <keels2/platform/file_fingerprint.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace keels2::compatibility_review
{

enum class ProfileStatus
{
    accepted,
    candidate_untrusted
};

enum class Severity
{
    information,
    warning,
    error
};

struct Diagnostic
{
    Severity severity{};
    std::string code;
    std::string message;
};

struct Report
{
    std::vector<Diagnostic> diagnostics;
    std::size_t changes{};
    bool review_required{};

    bool Ok() const noexcept;
    std::string Text() const;
};

struct Module
{
    std::string role;
    std::string name;
    platform::FileFingerprint fingerprint;
};

struct Interface
{
    std::string key;
    std::string name;
    std::string module;
};

struct Slot
{
    std::string interface_key;
    std::string method;
    std::uint32_t index{};
};

struct Pattern
{
    std::string key;
    std::string module;
    std::string expression;
    std::uint32_t occurrence{};
    std::uint32_t match_count{};
    std::uint64_t selected_file_offset{};
};

struct Profile
{
    ProfileStatus status{ProfileStatus::candidate_untrusted};
    std::string game;
    std::string version;
    std::string platform;
    std::vector<Module> modules;
    std::vector<Interface> interfaces;
    std::vector<Slot> slots;
    std::vector<Pattern> patterns;
};

struct ModuleInput
{
    std::string role;
    std::filesystem::path path;
};

struct PatternRequest
{
    std::string key;
    std::string module;
    std::string expression;
    std::uint32_t occurrence{};
};

struct CaptureRequest
{
    std::string game;
    std::string version;
    std::string platform;
    std::vector<ModuleInput> modules;
    std::vector<Interface> interfaces;
    std::vector<Slot> slots;
    std::vector<PatternRequest> patterns;
};

bool ReadCaptureRequest(
    const std::filesystem::path& path,
    CaptureRequest& request,
    Report& report);
bool ReadBindings(
    const std::filesystem::path& path,
    std::vector<ModuleInput>& bindings,
    Report& report);
bool ReadProfile(
    const std::filesystem::path& path,
    Profile& profile,
    Report& report);
bool WriteProfile(
    const std::filesystem::path& path,
    const Profile& profile,
    std::string& error);
bool Capture(
    const CaptureRequest& request,
    Profile& profile,
    Report& report);
Report Compare(const Profile& accepted, const Profile& candidate);
Report Validate(const Profile& profile, const std::vector<ModuleInput>& bindings);

}

#endif
