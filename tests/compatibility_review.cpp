#include "compatibility_review.h"

#include <keels2/cs2/compatibility.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace review = keels2::compatibility_review;

namespace
{

template <typename Value>
void WriteLittleEndian(std::vector<unsigned char>& bytes, std::size_t offset, Value value)
{
    for (std::size_t index{}; index < sizeof(Value); ++index)
    {
        bytes[offset + index] = static_cast<unsigned char>(
            (value >> static_cast<unsigned>(index * 8)) & static_cast<Value>(0xff));
    }
}

std::vector<unsigned char> Image(
    std::initializer_list<std::pair<std::size_t, std::vector<unsigned char>>> payloads)
{
    std::vector<unsigned char> bytes(512, 0);
    bytes[0] = 0x7f;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;
    WriteLittleEndian<std::uint16_t>(bytes, 18, 62);
    WriteLittleEndian<std::uint64_t>(bytes, 32, 64);
    WriteLittleEndian<std::uint16_t>(bytes, 54, 56);
    WriteLittleEndian<std::uint16_t>(bytes, 56, 1);
    WriteLittleEndian<std::uint32_t>(bytes, 64, 1);
    WriteLittleEndian<std::uint32_t>(bytes, 68, 5);
    WriteLittleEndian<std::uint64_t>(bytes, 72, 256);
    WriteLittleEndian<std::uint64_t>(bytes, 96, 128);
    for (const auto& [offset, payload] : payloads)
    {
        std::copy(payload.begin(), payload.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return bytes;
}

std::vector<unsigned char> PeImage(
    std::initializer_list<std::pair<std::size_t, std::vector<unsigned char>>> payloads)
{
    std::vector<unsigned char> bytes(768, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    WriteLittleEndian<std::uint32_t>(bytes, 0x3c, 64);
    bytes[64] = 'P';
    bytes[65] = 'E';
    WriteLittleEndian<std::uint16_t>(bytes, 68, 0x8664);
    WriteLittleEndian<std::uint16_t>(bytes, 70, 1);
    WriteLittleEndian<std::uint16_t>(bytes, 84, 240);
    WriteLittleEndian<std::uint16_t>(bytes, 88, 0x20b);
    constexpr std::size_t section = 328;
    WriteLittleEndian<std::uint32_t>(bytes, section + 16, 256);
    WriteLittleEndian<std::uint32_t>(bytes, section + 20, 512);
    WriteLittleEndian<std::uint32_t>(bytes, section + 36, 0x60000020);
    for (const auto& [offset, payload] : payloads)
    {
        std::copy(payload.begin(), payload.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return bytes;
}

bool Write(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

bool Write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    return static_cast<bool>(stream);
}

bool HasCode(const review::Report& report, const std::string& code)
{
    return std::any_of(report.diagnostics.begin(), report.diagnostics.end(), [&](const review::Diagnostic& diagnostic) {
        return diagnostic.code == code;
    });
}

review::CaptureRequest Request(
    const std::filesystem::path& server,
    const std::filesystem::path& engine,
    std::string platform = "linuxsteamrt64")
{
    review::CaptureRequest request;
    request.game = "cs2";
    request.version = "fixture-next";
    request.platform = std::move(platform);
    request.modules = {{"server", server}, {"engine", engine}};
    request.interfaces = {
        {"server", "Source2Server001", "server"},
        {"cvar", "VEngineCvar007", "engine"}
    };
    request.slots = {
        {"server", "Init", 3},
        {"server", "GameFrame", 19},
        {"cvar", "RegisterConCommand", 9}
    };
    request.patterns = {{"approved-fixture", "server", "DE AD ?? EF", 0}};
    return request;
}

bool BaselineMatches(const std::filesystem::path& path)
{
    review::Profile profile;
    review::Report report;
    if (!review::ReadProfile(path, profile, report) ||
        profile.status != review::ProfileStatus::accepted || profile.modules.size() != 1 ||
        profile.interfaces.size() != 4 || profile.slots.size() != 15 ||
        !profile.patterns.empty())
    {
        return false;
    }
    const auto* compiled = keels2::cs2::FindCompatibilityProfile(
        profile.modules[0].fingerprint,
        profile.platform.c_str());
    if (!compiled || profile.version != compiled->game_version ||
        profile.modules[0].role != "server" ||
        profile.modules[0].name != compiled->server_module ||
        profile.modules[0].fingerprint != compiled->server)
    {
        return false;
    }
    const auto slot = [&](const char* interface_key, const char* method) -> const review::Slot* {
        const auto found = std::find_if(profile.slots.begin(), profile.slots.end(), [&](const review::Slot& value) {
            return value.interface_key == interface_key && value.method == method;
        });
        return found == profile.slots.end() ? nullptr : &*found;
    };
    const auto interface_matches = [&](const char* key, const char* name) {
        const auto found = std::find_if(
            profile.interfaces.begin(),
            profile.interfaces.end(),
            [&](const review::Interface& value) {
                return value.key == key && value.name == name && value.module == "server";
            });
        return found != profile.interfaces.end();
    };
    const auto slot_matches = [&](const char* interface_key, const char* method, std::uint32_t index) {
        const auto* value = slot(interface_key, method);
        return value && value->index == index;
    };
    return interface_matches("server_config", compiled->server_config_interface) &&
        interface_matches("server", compiled->server_interface) &&
        interface_matches("game_clients", compiled->game_clients_interface) &&
        interface_matches("game_event_manager", compiled->game_event_manager_class) &&
        slot_matches("server_config", "Connect", compiled->connect_slot) &&
        slot_matches("server_config", "Disconnect", compiled->disconnect_slot) &&
        slot_matches("server", "Init", compiled->init_slot) &&
        slot_matches("game_clients", "Validation", compiled->game_clients_validation_slot) &&
        slot_matches("game_clients", "GameFrame", compiled->game_frame_slot) &&
        slot_matches("game_clients", "OnClientConnected", compiled->client_connected_slot) &&
        slot_matches("game_clients", "ClientPutInServer", compiled->client_put_in_server_slot) &&
        slot_matches("game_clients", "ClientActive", compiled->client_active_slot) &&
        slot_matches("game_clients", "ClientFullyConnected", compiled->client_fully_connected_slot) &&
        slot_matches("game_clients", "ClientDisconnecting", compiled->client_disconnecting_slot) &&
        slot_matches("game_clients", "ClientSettingsChanged", compiled->client_settings_changed_slot) &&
        slot_matches("game_clients", "ClientConnect", compiled->client_connect_slot) &&
        slot_matches("game_clients", "ClientCommand", compiled->client_command_slot) &&
        slot_matches("game_event_manager", "LoadEventsFromFile", compiled->game_event_load_events_slot) &&
        slot_matches("game_event_manager", "AddListener", compiled->game_event_add_listener_slot);
}

}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        return 1;
    }
    for (int argument = 2; argument < argc; ++argument)
    {
        if (!BaselineMatches(argv[argument]))
        {
            return 2;
        }
    }
    const std::filesystem::path root = argv[1];
    std::error_code filesystem_error;
    std::filesystem::remove_all(root, filesystem_error);
    if (!std::filesystem::create_directories(root, filesystem_error) || filesystem_error)
    {
        return 3;
    }
    const auto signature = std::vector<unsigned char>{0xde, 0xad, 0x11, 0xef};
    const std::filesystem::path server = root / "server.so";
    const std::filesystem::path engine = root / "engine.so";
    const auto server_image = Image({{300, signature}});
    const auto engine_image = Image({{330, {0xaa, 0xbb, 0xcc, 0xdd}}});
    if (!Write(server, server_image) || !Write(engine, engine_image))
    {
        return 4;
    }

    review::Profile candidate;
    review::Report capture_report;
    if (!review::Capture(Request(server, engine), candidate, capture_report) ||
        !capture_report.Ok() || !capture_report.review_required ||
        candidate.status != review::ProfileStatus::candidate_untrusted ||
        candidate.patterns.size() != 1 || candidate.patterns[0].match_count != 1 ||
        candidate.patterns[0].selected_file_offset != 300 ||
        !HasCode(capture_report, "human-review-required"))
    {
        return 5;
    }
    if (keels2::cs2::FindCompatibilityProfile(
            candidate.modules[0].fingerprint,
            candidate.platform.c_str()))
    {
        return 6;
    }

    const std::vector<review::ModuleInput> bindings{{"server", server}, {"engine", engine}};
    const review::Report validation = review::Validate(candidate, bindings);
    if (!validation.Ok() || !validation.review_required ||
        !HasCode(validation, "pattern-validated") ||
        !HasCode(validation, "human-review-required"))
    {
        return 7;
    }

    const std::filesystem::path candidate_path = root / "candidate.tsv";
    std::string write_error;
    if (!review::WriteProfile(candidate_path, candidate, write_error) ||
        review::WriteProfile(candidate_path, candidate, write_error))
    {
        return 7;
    }
    review::Profile round_trip;
    review::Report read_report;
    if (!review::ReadProfile(candidate_path, round_trip, read_report) ||
        round_trip.patterns[0].selected_file_offset != 300)
    {
        return 8;
    }

    review::Profile accepted = candidate;
    accepted.status = review::ProfileStatus::accepted;
    const review::Report unchanged = review::Compare(accepted, candidate);
    if (!unchanged.Ok() || unchanged.changes != 0 || !unchanged.review_required ||
        !HasCode(unchanged, "no-observed-differences"))
    {
        return 9;
    }
    review::Profile changed = candidate;
    changed.version = "fixture-later";
    changed.interfaces[0].name = "Source2Server002";
    changed.slots[0].index = 4;
    const review::Report differences = review::Compare(accepted, changed);
    if (!differences.Ok() || differences.changes != 3 ||
        !HasCode(differences, "version-changed") ||
        !HasCode(differences, "interface-changed") ||
        !HasCode(differences, "slot-changed"))
    {
        return 10;
    }

    auto tampered = server_image;
    tampered[301] ^= 0xff;
    if (!Write(server, tampered))
    {
        return 11;
    }
    const review::Report tampered_report = review::Validate(candidate, bindings);
    if (tampered_report.Ok() || !HasCode(tampered_report, "module-stale-or-tampered"))
    {
        return 12;
    }

    auto ambiguous = server_image;
    std::copy(signature.begin(), signature.end(), ambiguous.begin() + 320);
    if (!Write(server, ambiguous))
    {
        return 13;
    }
    review::Profile ambiguous_profile;
    review::Report ambiguous_report;
    if (review::Capture(Request(server, engine), ambiguous_profile, ambiguous_report) ||
        !HasCode(ambiguous_report, "pattern-resolution"))
    {
        return 14;
    }

    if (!Write(server, server_image))
    {
        return 15;
    }
    review::Profile cross_module = candidate;
    cross_module.patterns[0].module = "engine";
    const review::Report cross_module_report = review::Validate(cross_module, bindings);
    if (cross_module_report.Ok() || !HasCode(cross_module_report, "pattern-resolution"))
    {
        return 16;
    }

    const std::filesystem::path duplicate_request = root / "duplicate-request.tsv";
    if (!Write(
            duplicate_request,
            "keels2-compatibility-request\t1\n"
            "game\tcs2\nversion\tfixture\nplatform\tlinuxsteamrt64\n"
            "module\tserver\t" + server.string() + "\n"
            "module\tserver\t" + server.string() + "\n"))
    {
        return 17;
    }
    review::CaptureRequest duplicate;
    review::Report duplicate_report;
    if (review::ReadCaptureRequest(duplicate_request, duplicate, duplicate_report) ||
        !HasCode(duplicate_report, "duplicate-record"))
    {
        return 18;
    }

    const std::filesystem::path invalid_trust = root / "invalid-trust.tsv";
    if (!Write(
            invalid_trust,
            "keels2-compatibility-profile\t1\n"
            "status\taccepted\nreview\trequired\n"
            "game\tcs2\nversion\tfixture\nplatform\tlinuxsteamrt64\n"
            "module\tserver\tserver.so\t512\t0000000000000001\n"))
    {
        return 19;
    }
    review::Profile invalid;
    review::Report invalid_report;
    if (review::ReadProfile(invalid_trust, invalid, invalid_report) ||
        !HasCode(invalid_report, "profile-trust"))
    {
        return 20;
    }

    const std::filesystem::path pe_server = root / "server.dll";
    const std::filesystem::path pe_engine = root / "engine.dll";
    const auto pe_server_image = PeImage({{560, signature}});
    const auto pe_engine_image = PeImage({{600, {0xaa, 0xbb, 0xcc, 0xdd}}});
    if (!Write(pe_server, pe_server_image) || !Write(pe_engine, pe_engine_image))
    {
        return 21;
    }
    review::Profile pe_candidate;
    review::Report pe_capture;
    if (!review::Capture(Request(pe_server, pe_engine, "win64"), pe_candidate, pe_capture) ||
        !pe_capture.Ok() || pe_candidate.patterns.size() != 1 ||
        pe_candidate.patterns[0].selected_file_offset != 560)
    {
        return 22;
    }
    const review::Report pe_validation = review::Validate(
        pe_candidate,
        {{"server", pe_server}, {"engine", pe_engine}});
    if (!pe_validation.Ok() || !HasCode(pe_validation, "pattern-validated"))
    {
        return 23;
    }
    auto wrong_architecture = pe_server_image;
    WriteLittleEndian<std::uint16_t>(wrong_architecture, 68, 0x014c);
    if (!Write(pe_server, wrong_architecture))
    {
        return 24;
    }
    review::Profile wrong_architecture_candidate;
    review::Report wrong_architecture_report;
    if (review::Capture(
            Request(pe_server, pe_engine, "win64"),
            wrong_architecture_candidate,
            wrong_architecture_report) ||
        !HasCode(wrong_architecture_report, "module-image"))
    {
        return 25;
    }

    std::filesystem::remove_all(root, filesystem_error);
    return 0;
}
