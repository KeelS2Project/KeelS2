#include "compatibility_review.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace keels2::compatibility_review
{
namespace
{

using Record = std::vector<std::string>;

struct Range
{
    std::uint64_t offset{};
    std::uint64_t size{};
};

struct ModuleImage
{
    std::vector<unsigned char> bytes;
    std::vector<Range> executable_ranges;
};

void Add(
    Report& report,
    Severity severity,
    std::string code,
    std::string message)
{
    report.diagnostics.push_back({severity, std::move(code), std::move(message)});
}

bool ValidToken(std::string_view value)
{
    if (value.empty() || value.size() > 256)
    {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '_' || character == '-' ||
            character == '.' || character == ':';
    });
}

bool ValidText(std::string_view value, bool empty = false)
{
    if ((!empty && value.empty()) || value.size() > 16384)
    {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return character >= 32 && character != 127 && character != '\t';
    });
}

Record Split(std::string_view line)
{
    Record fields;
    std::size_t begin{};
    while (begin <= line.size())
    {
        const std::size_t end = line.find('\t', begin);
        fields.emplace_back(line.substr(
            begin,
            end == std::string_view::npos ? line.size() - begin : end - begin));
        if (end == std::string_view::npos)
        {
            break;
        }
        begin = end + 1;
    }
    return fields;
}

bool ReadRecords(
    const std::filesystem::path& path,
    std::string_view expected_header,
    std::vector<Record>& records,
    Report& report)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        Add(report, Severity::error, "input-open", "could not open " + path.string());
        return false;
    }
    std::string line;
    std::size_t line_number{};
    bool header{};
    while (std::getline(stream, line))
    {
        ++line_number;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        if (!header)
        {
            if (line != expected_header)
            {
                Add(
                    report,
                    Severity::error,
                    "input-header",
                    path.string() + ": expected header " + std::string(expected_header));
                return false;
            }
            header = true;
            continue;
        }
        Record fields = Split(line);
        if (fields.empty() || std::any_of(fields.begin(), fields.end(), [](const std::string& field) {
                return !ValidText(field, true);
            }))
        {
            Add(
                report,
                Severity::error,
                "input-record",
                path.string() + ": invalid record at line " + std::to_string(line_number));
            return false;
        }
        records.push_back(std::move(fields));
    }
    if (!stream.eof())
    {
        Add(report, Severity::error, "input-read", "could not read " + path.string());
        return false;
    }
    if (!header)
    {
        Add(report, Severity::error, "input-header", path.string() + ": missing header");
        return false;
    }
    return true;
}

template <typename Value>
bool ParseUnsigned(std::string_view text, Value& value)
{
    static_assert(std::is_unsigned_v<Value>);
    if (text.empty())
    {
        return false;
    }
    Value parsed{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    {
        return false;
    }
    value = parsed;
    return true;
}

bool ParseHex64(std::string_view text, std::uint64_t& value)
{
    if (text.size() != 16)
    {
        return false;
    }
    std::uint64_t parsed{};
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), parsed, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    {
        return false;
    }
    value = parsed;
    return true;
}

std::string Hex64(std::uint64_t value)
{
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

template <typename Value>
bool ReadLittleEndian(
    const std::vector<unsigned char>& bytes,
    std::uint64_t offset,
    Value& value)
{
    static_assert(std::is_unsigned_v<Value>);
    if (offset > bytes.size() || bytes.size() - static_cast<std::size_t>(offset) < sizeof(Value))
    {
        return false;
    }
    std::uint64_t result{};
    for (std::size_t index{}; index < sizeof(Value); ++index)
    {
        result |= static_cast<std::uint64_t>(bytes[static_cast<std::size_t>(offset) + index]) <<
            static_cast<unsigned>(index * 8);
    }
    value = static_cast<Value>(result);
    return true;
}

bool AddRange(
    std::vector<Range>& ranges,
    std::uint64_t offset,
    std::uint64_t size,
    std::size_t file_size)
{
    if (!size || offset > file_size || size > file_size - offset)
    {
        return false;
    }
    ranges.push_back({offset, size});
    return true;
}

bool ElfRanges(const std::vector<unsigned char>& bytes, std::vector<Range>& ranges)
{
    if (bytes.size() < 64 || bytes[0] != 0x7f || bytes[1] != 'E' ||
        bytes[2] != 'L' || bytes[3] != 'F' || bytes[4] != 2 || bytes[5] != 1 ||
        bytes[6] != 1)
    {
        return false;
    }
    std::uint16_t machine{};
    std::uint64_t table{};
    std::uint16_t entry_size{};
    std::uint16_t entry_count{};
    if (!ReadLittleEndian(bytes, 18, machine) || machine != 62 ||
        !ReadLittleEndian(bytes, 32, table) ||
        !ReadLittleEndian(bytes, 54, entry_size) ||
        !ReadLittleEndian(bytes, 56, entry_count) || entry_size < 56 || !entry_count)
    {
        return false;
    }
    if (table > bytes.size() ||
        static_cast<std::uint64_t>(entry_size) * entry_count > bytes.size() - table)
    {
        return false;
    }
    for (std::uint16_t index{}; index < entry_count; ++index)
    {
        const std::uint64_t entry = table + static_cast<std::uint64_t>(index) * entry_size;
        std::uint32_t type{};
        std::uint32_t flags{};
        std::uint64_t offset{};
        std::uint64_t size{};
        if (!ReadLittleEndian(bytes, entry, type) ||
            !ReadLittleEndian(bytes, entry + 4, flags) ||
            !ReadLittleEndian(bytes, entry + 8, offset) ||
            !ReadLittleEndian(bytes, entry + 32, size))
        {
            return false;
        }
        if (type == 1 && (flags & 1) != 0 && !AddRange(ranges, offset, size, bytes.size()))
        {
            return false;
        }
    }
    return !ranges.empty();
}

bool PeRanges(const std::vector<unsigned char>& bytes, std::vector<Range>& ranges)
{
    if (bytes.size() < 64 || bytes[0] != 'M' || bytes[1] != 'Z')
    {
        return false;
    }
    std::uint32_t pe_offset{};
    if (!ReadLittleEndian(bytes, 0x3c, pe_offset) || pe_offset > bytes.size() ||
        bytes.size() - pe_offset < 24 || bytes[pe_offset] != 'P' ||
        bytes[pe_offset + 1] != 'E' || bytes[pe_offset + 2] != 0 ||
        bytes[pe_offset + 3] != 0)
    {
        return false;
    }
    std::uint16_t machine{};
    std::uint16_t section_count{};
    std::uint16_t optional_size{};
    std::uint16_t optional_magic{};
    if (!ReadLittleEndian(bytes, static_cast<std::uint64_t>(pe_offset) + 4, machine) ||
        machine != 0x8664 ||
        !ReadLittleEndian(bytes, static_cast<std::uint64_t>(pe_offset) + 6, section_count) ||
        !ReadLittleEndian(bytes, static_cast<std::uint64_t>(pe_offset) + 20, optional_size) ||
        optional_size < sizeof(optional_magic) ||
        !ReadLittleEndian(bytes, static_cast<std::uint64_t>(pe_offset) + 24, optional_magic) ||
        optional_magic != 0x20b || !section_count)
    {
        return false;
    }
    const std::uint64_t table = static_cast<std::uint64_t>(pe_offset) + 24 + optional_size;
    if (table > bytes.size() || static_cast<std::uint64_t>(section_count) * 40 > bytes.size() - table)
    {
        return false;
    }
    for (std::uint16_t index{}; index < section_count; ++index)
    {
        const std::uint64_t section = table + static_cast<std::uint64_t>(index) * 40;
        std::uint32_t raw_size{};
        std::uint32_t raw_offset{};
        std::uint32_t characteristics{};
        if (!ReadLittleEndian(bytes, section + 16, raw_size) ||
            !ReadLittleEndian(bytes, section + 20, raw_offset) ||
            !ReadLittleEndian(bytes, section + 36, characteristics))
        {
            return false;
        }
        if ((characteristics & 0x20000000u) != 0 &&
            !AddRange(ranges, raw_offset, raw_size, bytes.size()))
        {
            return false;
        }
    }
    return !ranges.empty();
}

bool ReadModuleImage(
    const std::filesystem::path& path,
    ModuleImage& image,
    std::string& error)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
    {
        error = "could not open module";
        return false;
    }
    const std::streamoff end = stream.tellg();
    if (end <= 0 || static_cast<std::uint64_t>(end) >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        error = "module size is invalid";
        return false;
    }
    image.bytes.resize(static_cast<std::size_t>(end));
    stream.seekg(0);
    stream.read(
        reinterpret_cast<char*>(image.bytes.data()),
        static_cast<std::streamsize>(image.bytes.size()));
    if (!stream || static_cast<std::size_t>(stream.gcount()) != image.bytes.size())
    {
        error = "could not read the complete module";
        return false;
    }
    image.executable_ranges.clear();
    if (!ElfRanges(image.bytes, image.executable_ranges) &&
        !PeRanges(image.bytes, image.executable_ranges))
    {
        error = "module is not a supported ELF64 or PE image with executable file ranges";
        return false;
    }
    std::sort(image.executable_ranges.begin(), image.executable_ranges.end(), [](const Range& left, const Range& right) {
        return left.offset < right.offset;
    });
    error.clear();
    return true;
}

int HexDigit(unsigned char value)
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    value = static_cast<unsigned char>(std::tolower(value));
    return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
}

bool ParsePattern(
    std::string_view text,
    std::vector<std::optional<std::uint8_t>>& pattern,
    std::string& normalized)
{
    std::size_t position{};
    std::ostringstream output;
    while (position < text.size())
    {
        while (position < text.size() &&
            std::isspace(static_cast<unsigned char>(text[position])))
        {
            ++position;
        }
        if (position == text.size())
        {
            break;
        }
        const std::size_t begin = position;
        while (position < text.size() &&
            !std::isspace(static_cast<unsigned char>(text[position])))
        {
            ++position;
        }
        const std::string_view token = text.substr(begin, position - begin);
        if (!pattern.empty())
        {
            output << ' ';
        }
        if (token == "?" || token == "??")
        {
            pattern.emplace_back(std::nullopt);
            output << "??";
        }
        else
        {
            if (token.size() != 2)
            {
                return false;
            }
            const int high = HexDigit(static_cast<unsigned char>(token[0]));
            const int low = HexDigit(static_cast<unsigned char>(token[1]));
            if (high < 0 || low < 0)
            {
                return false;
            }
            const auto value = static_cast<std::uint8_t>((high << 4) | low);
            pattern.emplace_back(value);
            output << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(value);
        }
        if (pattern.size() > 4096)
        {
            return false;
        }
    }
    normalized = output.str();
    return !pattern.empty();
}

std::vector<std::uint64_t> Scan(
    const ModuleImage& image,
    const std::vector<std::optional<std::uint8_t>>& pattern)
{
    std::vector<std::uint64_t> matches;
    for (const Range& range : image.executable_ranges)
    {
        if (range.size < pattern.size())
        {
            continue;
        }
        const std::uint64_t last = range.offset + range.size - pattern.size();
        for (std::uint64_t position = range.offset; position <= last; ++position)
        {
            bool match = true;
            for (std::size_t index{}; index < pattern.size(); ++index)
            {
                if (pattern[index] && image.bytes[static_cast<std::size_t>(position) + index] != *pattern[index])
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                matches.push_back(position);
            }
        }
    }
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    return matches;
}

bool SelectMatch(
    const std::vector<std::uint64_t>& matches,
    std::uint32_t occurrence,
    std::uint64_t& selected,
    std::string& reason)
{
    if (matches.empty())
    {
        reason = "was not found in an executable file range";
        return false;
    }
    if (!occurrence)
    {
        if (matches.size() != 1)
        {
            reason = "is ambiguous with " + std::to_string(matches.size()) + " matches";
            return false;
        }
        selected = matches.front();
        return true;
    }
    if (occurrence > matches.size())
    {
        reason = "requested occurrence " + std::to_string(occurrence) +
            " but only " + std::to_string(matches.size()) + " matches exist";
        return false;
    }
    selected = matches[occurrence - 1];
    return true;
}

bool UniqueInsert(
    std::set<std::string>& values,
    const std::string& value,
    Report& report,
    std::string_view kind)
{
    if (!ValidToken(value))
    {
        Add(report, Severity::error, "invalid-identifier", std::string(kind) + " identifier is invalid: " + value);
        return false;
    }
    if (!values.insert(value).second)
    {
        Add(report, Severity::error, "duplicate-record", "duplicate " + std::string(kind) + ": " + value);
        return false;
    }
    return true;
}

bool ValidateStructure(const Profile& profile, Report& report)
{
    if (!ValidToken(profile.game) || !ValidToken(profile.version) || !ValidToken(profile.platform))
    {
        Add(report, Severity::error, "profile-metadata", "game, version, or platform is invalid");
        return false;
    }
    std::set<std::string> modules;
    std::set<std::string> interfaces;
    std::set<std::string> slots;
    std::set<std::string> patterns;
    bool valid = true;
    for (const Module& module : profile.modules)
    {
        valid = UniqueInsert(modules, module.role, report, "module role") && valid;
        if (!ValidText(module.name) || module.fingerprint.size == 0)
        {
            Add(report, Severity::error, "module-record", "module record is invalid: " + module.role);
            valid = false;
        }
    }
    for (const Interface& interface_value : profile.interfaces)
    {
        valid = UniqueInsert(interfaces, interface_value.key, report, "interface key") && valid;
        if (!ValidToken(interface_value.name) || !modules.contains(interface_value.module))
        {
            Add(report, Severity::error, "interface-record", "interface record is invalid: " + interface_value.key);
            valid = false;
        }
    }
    for (const Slot& slot : profile.slots)
    {
        const std::string key = slot.interface_key + "\t" + slot.method;
        if (!slots.insert(key).second)
        {
            Add(report, Severity::error, "duplicate-record", "duplicate slot: " + slot.interface_key + "." + slot.method);
            valid = false;
        }
        if (!ValidToken(slot.interface_key) || !interfaces.contains(slot.interface_key) ||
            !ValidToken(slot.method))
        {
            Add(report, Severity::error, "slot-record", "slot record is invalid: " + slot.interface_key + "." + slot.method);
            valid = false;
        }
    }
    for (const Pattern& pattern : profile.patterns)
    {
        valid = UniqueInsert(patterns, pattern.key, report, "pattern key") && valid;
        std::vector<std::optional<std::uint8_t>> parsed;
        std::string normalized;
        if (!modules.contains(pattern.module) ||
            !ParsePattern(pattern.expression, parsed, normalized) ||
            normalized != pattern.expression || !pattern.match_count ||
            (pattern.occurrence && pattern.occurrence > pattern.match_count))
        {
            Add(report, Severity::error, "pattern-record", "pattern record is invalid: " + pattern.key);
            valid = false;
        }
    }
    return valid;
}

template <typename Value, typename Key>
std::map<std::string, const Value*> IndexBy(
    const std::vector<Value>& values,
    Key key)
{
    std::map<std::string, const Value*> result;
    for (const Value& value : values)
    {
        result.emplace(key(value), &value);
    }
    return result;
}

void Difference(Report& report, std::string code, std::string message)
{
    ++report.changes;
    Add(report, Severity::warning, std::move(code), std::move(message));
}

}

bool Report::Ok() const noexcept
{
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == Severity::error;
    });
}

std::string Report::Text() const
{
    std::ostringstream stream;
    for (const Diagnostic& diagnostic : diagnostics)
    {
        const char* severity = diagnostic.severity == Severity::information
            ? "INFO"
            : diagnostic.severity == Severity::warning ? "WARNING" : "ERROR";
        stream << severity << '\t' << diagnostic.code << '\t' << diagnostic.message << '\n';
    }
    stream << "CHANGES\t" << changes << '\n';
    stream << "RESULT\t";
    if (!Ok())
    {
        stream << "FAIL";
    }
    else if (review_required)
    {
        stream << "PASS_UNTRUSTED_REVIEW_REQUIRED";
    }
    else
    {
        stream << "PASS";
    }
    stream << '\n';
    return stream.str();
}

bool ReadCaptureRequest(
    const std::filesystem::path& path,
    CaptureRequest& request,
    Report& report)
{
    request = {};
    std::vector<Record> records;
    if (!ReadRecords(path, "keels2-compatibility-request\t1", records, report))
    {
        return false;
    }
    std::set<std::string> scalars;
    std::set<std::string> modules;
    std::set<std::string> interfaces;
    std::set<std::string> slots;
    std::set<std::string> patterns;
    for (const Record& fields : records)
    {
        if (fields[0] == "game" || fields[0] == "version" || fields[0] == "platform")
        {
            if (fields.size() != 2 || !ValidToken(fields[1]) || !scalars.insert(fields[0]).second)
            {
                Add(report, Severity::error, "request-scalar", "invalid or duplicate " + fields[0] + " record");
                continue;
            }
            std::string* target = fields[0] == "game"
                ? &request.game
                : fields[0] == "version" ? &request.version : &request.platform;
            *target = fields[1];
        }
        else if (fields[0] == "module")
        {
            if (fields.size() != 3 || !UniqueInsert(modules, fields[1], report, "module role") || fields[2].empty())
            {
                Add(report, Severity::error, "request-module", "invalid module request");
                continue;
            }
            request.modules.push_back({fields[1], std::filesystem::path(fields[2])});
        }
        else if (fields[0] == "interface")
        {
            if (fields.size() != 4 || !UniqueInsert(interfaces, fields[1], report, "interface key") ||
                !ValidToken(fields[2]) || !ValidToken(fields[3]))
            {
                Add(report, Severity::error, "request-interface", "invalid interface request");
                continue;
            }
            request.interfaces.push_back({fields[1], fields[2], fields[3]});
        }
        else if (fields[0] == "slot")
        {
            std::uint32_t index{};
            const std::string key = fields.size() >= 3 ? fields[1] + "\t" + fields[2] : std::string{};
            if (fields.size() != 4 || !slots.insert(key).second ||
                !ValidToken(fields[1]) || !ValidToken(fields[2]) || !ParseUnsigned(fields[3], index))
            {
                Add(report, Severity::error, "request-slot", "invalid slot request");
                continue;
            }
            request.slots.push_back({fields[1], fields[2], index});
        }
        else if (fields[0] == "pattern")
        {
            std::uint32_t occurrence{};
            std::vector<std::optional<std::uint8_t>> parsed;
            std::string normalized;
            if (fields.size() != 5 || !UniqueInsert(patterns, fields[1], report, "pattern key") ||
                !ValidToken(fields[2]) || !ParsePattern(fields[3], parsed, normalized) ||
                !ParseUnsigned(fields[4], occurrence))
            {
                Add(report, Severity::error, "request-pattern", "invalid pattern request");
                continue;
            }
            request.patterns.push_back({fields[1], fields[2], normalized, occurrence});
        }
        else
        {
            Add(report, Severity::error, "request-record", "unknown request record: " + fields[0]);
        }
    }
    if (scalars.size() != 3 || request.modules.empty())
    {
        Add(report, Severity::error, "request-required", "request requires game, version, platform, and at least one module");
    }
    for (const Interface& interface_value : request.interfaces)
    {
        if (!modules.contains(interface_value.module))
        {
            Add(report, Severity::error, "request-interface-module", "interface " + interface_value.key + " names unknown module " + interface_value.module);
        }
    }
    for (const Slot& slot : request.slots)
    {
        if (!interfaces.contains(slot.interface_key))
        {
            Add(report, Severity::error, "request-slot-interface", "slot names unknown interface " + slot.interface_key);
        }
    }
    for (const PatternRequest& pattern : request.patterns)
    {
        if (!modules.contains(pattern.module))
        {
            Add(report, Severity::error, "request-pattern-module", "pattern " + pattern.key + " names unknown module " + pattern.module);
        }
    }
    return report.Ok();
}

bool ReadBindings(
    const std::filesystem::path& path,
    std::vector<ModuleInput>& bindings,
    Report& report)
{
    bindings.clear();
    std::vector<Record> records;
    if (!ReadRecords(path, "keels2-compatibility-bindings\t1", records, report))
    {
        return false;
    }
    std::set<std::string> roles;
    for (const Record& fields : records)
    {
        if (fields.size() != 3 || fields[0] != "module" ||
            !UniqueInsert(roles, fields[1], report, "module role") || fields[2].empty())
        {
            Add(report, Severity::error, "binding-record", "invalid module binding");
            continue;
        }
        bindings.push_back({fields[1], std::filesystem::path(fields[2])});
    }
    if (bindings.empty())
    {
        Add(report, Severity::error, "binding-required", "at least one module binding is required");
    }
    return report.Ok();
}

bool ReadProfile(
    const std::filesystem::path& path,
    Profile& profile,
    Report& report)
{
    profile = {};
    std::vector<Record> records;
    if (!ReadRecords(path, "keels2-compatibility-profile\t1", records, report))
    {
        return false;
    }
    std::map<std::string, std::string> scalars;
    for (const Record& fields : records)
    {
        if (fields[0] == "status" || fields[0] == "review" || fields[0] == "game" ||
            fields[0] == "version" || fields[0] == "platform")
        {
            if (fields.size() != 2 || !scalars.emplace(fields[0], fields[1]).second)
            {
                Add(report, Severity::error, "profile-scalar", "invalid or duplicate " + fields[0] + " record");
            }
        }
        else if (fields[0] == "module")
        {
            Module module;
            if (fields.size() != 5 || !ParseUnsigned(fields[3], module.fingerprint.size) ||
                !ParseHex64(fields[4], module.fingerprint.fnv1a64))
            {
                Add(report, Severity::error, "profile-module", "invalid module record");
                continue;
            }
            module.role = fields[1];
            module.name = fields[2];
            profile.modules.push_back(std::move(module));
        }
        else if (fields[0] == "interface")
        {
            if (fields.size() != 4)
            {
                Add(report, Severity::error, "profile-interface", "invalid interface record");
                continue;
            }
            profile.interfaces.push_back({fields[1], fields[2], fields[3]});
        }
        else if (fields[0] == "slot")
        {
            std::uint32_t index{};
            if (fields.size() != 4 || !ParseUnsigned(fields[3], index))
            {
                Add(report, Severity::error, "profile-slot", "invalid slot record");
                continue;
            }
            profile.slots.push_back({fields[1], fields[2], index});
        }
        else if (fields[0] == "pattern")
        {
            Pattern pattern;
            if (fields.size() != 7 || !ParseUnsigned(fields[4], pattern.occurrence) ||
                !ParseUnsigned(fields[5], pattern.match_count) ||
                !ParseUnsigned(fields[6], pattern.selected_file_offset))
            {
                Add(report, Severity::error, "profile-pattern", "invalid pattern record");
                continue;
            }
            pattern.key = fields[1];
            pattern.module = fields[2];
            pattern.expression = fields[3];
            profile.patterns.push_back(std::move(pattern));
        }
        else
        {
            Add(report, Severity::error, "profile-record", "unknown profile record: " + fields[0]);
        }
    }
    const auto status = scalars.find("status");
    const auto review = scalars.find("review");
    if (status == scalars.end() || review == scalars.end() ||
        !scalars.contains("game") || !scalars.contains("version") || !scalars.contains("platform"))
    {
        Add(report, Severity::error, "profile-required", "profile is missing required scalar records");
        return false;
    }
    if (status->second == "accepted" && review->second == "complete")
    {
        profile.status = ProfileStatus::accepted;
    }
    else if (status->second == "candidate-untrusted" && review->second == "required")
    {
        profile.status = ProfileStatus::candidate_untrusted;
    }
    else
    {
        Add(report, Severity::error, "profile-trust", "status and review records do not form a valid trust state");
    }
    profile.game = scalars["game"];
    profile.version = scalars["version"];
    profile.platform = scalars["platform"];
    ValidateStructure(profile, report);
    return report.Ok();
}

bool WriteProfile(
    const std::filesystem::path& path,
    const Profile& profile,
    std::string& error)
{
    Report structure;
    if (!ValidateStructure(profile, structure))
    {
        error = structure.Text();
        return false;
    }
    if (std::filesystem::exists(path))
    {
        error = "refusing to overwrite existing profile: " + path.string();
        return false;
    }
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "could not create profile: " + path.string();
        return false;
    }
    stream << "keels2-compatibility-profile\t1\n";
    if (profile.status == ProfileStatus::accepted)
    {
        stream << "status\taccepted\nreview\tcomplete\n";
    }
    else
    {
        stream << "status\tcandidate-untrusted\nreview\trequired\n";
    }
    stream << "game\t" << profile.game << "\nversion\t" << profile.version
        << "\nplatform\t" << profile.platform << '\n';
    auto modules = profile.modules;
    auto interfaces = profile.interfaces;
    auto slots = profile.slots;
    auto patterns = profile.patterns;
    std::sort(modules.begin(), modules.end(), [](const Module& left, const Module& right) {
        return left.role < right.role;
    });
    std::sort(interfaces.begin(), interfaces.end(), [](const Interface& left, const Interface& right) {
        return left.key < right.key;
    });
    std::sort(slots.begin(), slots.end(), [](const Slot& left, const Slot& right) {
        return std::tie(left.interface_key, left.method) < std::tie(right.interface_key, right.method);
    });
    std::sort(patterns.begin(), patterns.end(), [](const Pattern& left, const Pattern& right) {
        return left.key < right.key;
    });
    for (const Module& module : modules)
    {
        stream << "module\t" << module.role << '\t' << module.name << '\t'
            << module.fingerprint.size << '\t' << Hex64(module.fingerprint.fnv1a64) << '\n';
    }
    for (const Interface& interface_value : interfaces)
    {
        stream << "interface\t" << interface_value.key << '\t' << interface_value.name
            << '\t' << interface_value.module << '\n';
    }
    for (const Slot& slot : slots)
    {
        stream << "slot\t" << slot.interface_key << '\t' << slot.method << '\t'
            << slot.index << '\n';
    }
    for (const Pattern& pattern : patterns)
    {
        stream << "pattern\t" << pattern.key << '\t' << pattern.module << '\t'
            << pattern.expression << '\t' << pattern.occurrence << '\t'
            << pattern.match_count << '\t' << pattern.selected_file_offset << '\n';
    }
    stream.flush();
    if (!stream)
    {
        error = "could not write complete profile: " + path.string();
        return false;
    }
    error.clear();
    return true;
}

bool Capture(
    const CaptureRequest& request,
    Profile& profile,
    Report& report)
{
    report.review_required = true;
    profile = {};
    profile.status = ProfileStatus::candidate_untrusted;
    profile.game = request.game;
    profile.version = request.version;
    profile.platform = request.platform;
    profile.interfaces = request.interfaces;
    profile.slots = request.slots;
    std::map<std::string, ModuleImage> images;
    for (const ModuleInput& input : request.modules)
    {
        platform::FileFingerprint fingerprint;
        std::string error;
        if (!platform::FingerprintFile(input.path, fingerprint, error))
        {
            Add(report, Severity::error, "module-fingerprint", input.role + ": " + error + " (" + input.path.string() + ")");
            continue;
        }
        ModuleImage image;
        if (!ReadModuleImage(input.path, image, error))
        {
            Add(report, Severity::error, "module-image", input.role + ": " + error + " (" + input.path.string() + ")");
            continue;
        }
        profile.modules.push_back({input.role, input.path.filename().string(), fingerprint});
        images.emplace(input.role, std::move(image));
        Add(report, Severity::information, "module-captured", input.role + " " + platform::FormatFingerprint(fingerprint));
    }
    for (const PatternRequest& request_pattern : request.patterns)
    {
        const auto image = images.find(request_pattern.module);
        if (image == images.end())
        {
            Add(report, Severity::error, "pattern-module", request_pattern.key + ": module was not captured: " + request_pattern.module);
            continue;
        }
        std::vector<std::optional<std::uint8_t>> parsed;
        std::string normalized;
        if (!ParsePattern(request_pattern.expression, parsed, normalized))
        {
            Add(report, Severity::error, "pattern-syntax", request_pattern.key + ": pattern syntax is invalid");
            continue;
        }
        const auto matches = Scan(image->second, parsed);
        std::uint64_t selected{};
        std::string reason;
        if (!SelectMatch(matches, request_pattern.occurrence, selected, reason))
        {
            Add(report, Severity::error, "pattern-resolution", request_pattern.key + " " + reason + " in module " + request_pattern.module);
            continue;
        }
        if (matches.size() > std::numeric_limits<std::uint32_t>::max())
        {
            Add(report, Severity::error, "pattern-count", request_pattern.key + ": match count exceeds the profile format");
            continue;
        }
        profile.patterns.push_back({
            request_pattern.key,
            request_pattern.module,
            normalized,
            request_pattern.occurrence,
            static_cast<std::uint32_t>(matches.size()),
            selected
        });
        Add(
            report,
            Severity::information,
            "pattern-captured",
            request_pattern.key + " module=" + request_pattern.module +
                " matches=" + std::to_string(matches.size()) +
                " selected_file_offset=" + std::to_string(selected));
    }
    ValidateStructure(profile, report);
    if (report.Ok())
    {
        Add(
            report,
            Severity::warning,
            "human-review-required",
            "candidate evidence is untrusted; review it and update the compiled exact profile manually");
    }
    return report.Ok();
}

Report Compare(const Profile& accepted, const Profile& candidate)
{
    Report report;
    report.review_required = true;
    if (accepted.status != ProfileStatus::accepted)
    {
        Add(report, Severity::error, "accepted-trust", "accepted input is not marked accepted with review complete");
    }
    if (candidate.status != ProfileStatus::candidate_untrusted)
    {
        Add(report, Severity::error, "candidate-trust", "candidate input is not marked candidate-untrusted with review required");
    }
    Report structure;
    ValidateStructure(accepted, structure);
    ValidateStructure(candidate, structure);
    for (const Diagnostic& diagnostic : structure.diagnostics)
    {
        report.diagnostics.push_back(diagnostic);
    }
    if (accepted.game != candidate.game)
    {
        Add(report, Severity::error, "game-mismatch", "accepted game " + accepted.game + " differs from candidate game " + candidate.game);
    }
    if (accepted.platform != candidate.platform)
    {
        Add(report, Severity::error, "platform-mismatch", "accepted platform " + accepted.platform + " differs from candidate platform " + candidate.platform);
    }
    if (accepted.version != candidate.version)
    {
        Difference(report, "version-changed", accepted.version + " -> " + candidate.version);
    }
    const auto accepted_modules = IndexBy(accepted.modules, [](const Module& value) { return value.role; });
    const auto candidate_modules = IndexBy(candidate.modules, [](const Module& value) { return value.role; });
    for (const auto& [key, value] : accepted_modules)
    {
        const auto other = candidate_modules.find(key);
        if (other == candidate_modules.end())
        {
            Difference(report, "module-removed", key);
        }
        else if (value->name != other->second->name || value->fingerprint != other->second->fingerprint)
        {
            Difference(
                report,
                "module-changed",
                key + " " + value->name + " " + platform::FormatFingerprint(value->fingerprint) +
                    " -> " + other->second->name + " " + platform::FormatFingerprint(other->second->fingerprint));
        }
    }
    for (const auto& [key, value] : candidate_modules)
    {
        if (!accepted_modules.contains(key))
        {
            Difference(report, "module-added", key + " " + value->name);
        }
    }
    const auto accepted_interfaces = IndexBy(accepted.interfaces, [](const Interface& value) { return value.key; });
    const auto candidate_interfaces = IndexBy(candidate.interfaces, [](const Interface& value) { return value.key; });
    for (const auto& [key, value] : accepted_interfaces)
    {
        const auto other = candidate_interfaces.find(key);
        if (other == candidate_interfaces.end())
        {
            Difference(report, "interface-removed", key);
        }
        else if (value->name != other->second->name || value->module != other->second->module)
        {
            Difference(report, "interface-changed", key + " " + value->name + "@" + value->module +
                " -> " + other->second->name + "@" + other->second->module);
        }
    }
    for (const auto& [key, value] : candidate_interfaces)
    {
        if (!accepted_interfaces.contains(key))
        {
            Difference(report, "interface-added", key + " " + value->name + "@" + value->module);
        }
    }
    const auto slot_key = [](const Slot& value) { return value.interface_key + "." + value.method; };
    const auto accepted_slots = IndexBy(accepted.slots, slot_key);
    const auto candidate_slots = IndexBy(candidate.slots, slot_key);
    for (const auto& [key, value] : accepted_slots)
    {
        const auto other = candidate_slots.find(key);
        if (other == candidate_slots.end())
        {
            Difference(report, "slot-removed", key);
        }
        else if (value->index != other->second->index)
        {
            Difference(report, "slot-changed", key + " " + std::to_string(value->index) + " -> " + std::to_string(other->second->index));
        }
    }
    for (const auto& [key, value] : candidate_slots)
    {
        if (!accepted_slots.contains(key))
        {
            Difference(report, "slot-added", key + "=" + std::to_string(value->index));
        }
    }
    const auto accepted_patterns = IndexBy(accepted.patterns, [](const Pattern& value) { return value.key; });
    const auto candidate_patterns = IndexBy(candidate.patterns, [](const Pattern& value) { return value.key; });
    for (const auto& [key, value] : accepted_patterns)
    {
        const auto other = candidate_patterns.find(key);
        if (other == candidate_patterns.end())
        {
            Difference(report, "pattern-removed", key);
        }
        else if (value->module != other->second->module ||
            value->expression != other->second->expression ||
            value->occurrence != other->second->occurrence ||
            value->match_count != other->second->match_count ||
            value->selected_file_offset != other->second->selected_file_offset)
        {
            Difference(report, "pattern-changed", key + " requires signature review");
        }
    }
    for (const auto& [key, value] : candidate_patterns)
    {
        if (!accepted_patterns.contains(key))
        {
            Difference(report, "pattern-added", key + "@" + value->module + " requires signature review");
        }
    }
    if (!report.changes && report.Ok())
    {
        Add(report, Severity::information, "no-observed-differences", "accepted and candidate evidence fields are identical");
    }
    Add(report, Severity::warning, "human-review-required", "comparison never promotes or trusts the candidate");
    return report;
}

Report Validate(const Profile& profile, const std::vector<ModuleInput>& bindings)
{
    Report report;
    report.review_required = profile.status == ProfileStatus::candidate_untrusted;
    ValidateStructure(profile, report);
    std::map<std::string, std::filesystem::path> binding_map;
    for (const ModuleInput& binding : bindings)
    {
        if (!ValidToken(binding.role) || binding.path.empty() ||
            !binding_map.emplace(binding.role, binding.path).second)
        {
            Add(report, Severity::error, "binding-invalid", "invalid or duplicate module binding: " + binding.role);
        }
    }
    const auto modules = IndexBy(profile.modules, [](const Module& value) { return value.role; });
    for (const auto& [role, path] : binding_map)
    {
        if (!modules.contains(role))
        {
            Add(report, Severity::error, "binding-extra", "binding names a module role absent from the profile: " + role);
        }
    }
    std::map<std::string, ModuleImage> images;
    for (const auto& [role, module] : modules)
    {
        const auto binding = binding_map.find(role);
        if (binding == binding_map.end())
        {
            Add(report, Severity::error, "binding-missing", "missing binding for module role: " + role);
            continue;
        }
        if (binding->second.filename().string() != module->name)
        {
            Add(report, Severity::error, "module-name", role + ": expected filename " + module->name + " but received " + binding->second.filename().string());
            continue;
        }
        platform::FileFingerprint fingerprint;
        std::string error;
        if (!platform::FingerprintFile(binding->second, fingerprint, error))
        {
            Add(report, Severity::error, "module-fingerprint", role + ": " + error);
            continue;
        }
        if (fingerprint != module->fingerprint)
        {
            Add(report, Severity::error, "module-stale-or-tampered", role + ": expected " +
                platform::FormatFingerprint(module->fingerprint) + " but found " +
                platform::FormatFingerprint(fingerprint));
            continue;
        }
        ModuleImage image;
        if (!ReadModuleImage(binding->second, image, error))
        {
            Add(report, Severity::error, "module-image", role + ": " + error);
            continue;
        }
        images.emplace(role, std::move(image));
        Add(report, Severity::information, "module-validated", role + " " + platform::FormatFingerprint(fingerprint));
    }
    for (const Pattern& pattern : profile.patterns)
    {
        const auto image = images.find(pattern.module);
        if (image == images.end())
        {
            Add(report, Severity::error, "pattern-cross-module", pattern.key + ": declared module was not validated: " + pattern.module);
            continue;
        }
        std::vector<std::optional<std::uint8_t>> parsed;
        std::string normalized;
        if (!ParsePattern(pattern.expression, parsed, normalized))
        {
            Add(report, Severity::error, "pattern-syntax", pattern.key + ": invalid stored pattern");
            continue;
        }
        const auto matches = Scan(image->second, parsed);
        std::uint64_t selected{};
        std::string reason;
        if (!SelectMatch(matches, pattern.occurrence, selected, reason))
        {
            Add(report, Severity::error, "pattern-resolution", pattern.key + " " + reason + " in declared module " + pattern.module);
            continue;
        }
        if (matches.size() != pattern.match_count || selected != pattern.selected_file_offset)
        {
            Add(report, Severity::error, "pattern-stale", pattern.key + ": stored match count or selected file offset changed");
            continue;
        }
        Add(report, Severity::information, "pattern-validated", pattern.key + " module=" + pattern.module +
            " selected_file_offset=" + std::to_string(selected));
    }
    if (report.Ok() && report.review_required)
    {
        Add(report, Severity::warning, "human-review-required", "validation proves evidence consistency but does not trust the candidate");
    }
    return report;
}

}
