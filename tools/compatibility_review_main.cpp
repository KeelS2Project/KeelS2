#include "compatibility_review.h"

#include <iostream>
#include <string_view>

namespace review = keels2::compatibility_review;

namespace
{

int Usage()
{
    std::cerr
        << "usage:\n"
        << "  keels2_compatibility_review capture <request.tsv> <candidate.tsv>\n"
        << "  keels2_compatibility_review compare <accepted.tsv> <candidate.tsv>\n"
        << "  keels2_compatibility_review validate <profile.tsv> <bindings.tsv>\n";
    return 64;
}

bool ReadProfile(const char* path, review::Profile& profile)
{
    review::Report report;
    if (!review::ReadProfile(path, profile, report))
    {
        std::cout << report.Text();
        return false;
    }
    return true;
}

int Capture(const char* request_path, const char* output_path)
{
    review::CaptureRequest request;
    review::Report request_report;
    if (!review::ReadCaptureRequest(request_path, request, request_report))
    {
        std::cout << request_report.Text();
        return 1;
    }
    review::Profile profile;
    review::Report capture_report;
    if (!review::Capture(request, profile, capture_report))
    {
        std::cout << capture_report.Text();
        return 1;
    }
    std::string error;
    if (!review::WriteProfile(output_path, profile, error))
    {
        std::cerr << "ERROR\tprofile-write\t" << error << '\n';
        return 1;
    }
    std::cout << capture_report.Text();
    return 0;
}

int Compare(const char* accepted_path, const char* candidate_path)
{
    review::Profile accepted;
    review::Profile candidate;
    if (!ReadProfile(accepted_path, accepted) || !ReadProfile(candidate_path, candidate))
    {
        return 1;
    }
    const review::Report report = review::Compare(accepted, candidate);
    std::cout << report.Text();
    if (!report.Ok())
    {
        return 1;
    }
    return report.changes ? 2 : 0;
}

int Validate(const char* profile_path, const char* bindings_path)
{
    review::Profile profile;
    if (!ReadProfile(profile_path, profile))
    {
        return 1;
    }
    std::vector<review::ModuleInput> bindings;
    review::Report binding_report;
    if (!review::ReadBindings(bindings_path, bindings, binding_report))
    {
        std::cout << binding_report.Text();
        return 1;
    }
    const review::Report report = review::Validate(profile, bindings);
    std::cout << report.Text();
    return report.Ok() ? 0 : 1;
}

}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        return Usage();
    }
    if (argc != 4)
    {
        return Usage();
    }
    const std::string_view command = argv[1];
    if (command == "capture")
    {
        return Capture(argv[2], argv[3]);
    }
    if (command == "compare")
    {
        return Compare(argv[2], argv[3]);
    }
    if (command == "validate")
    {
        return Validate(argv[2], argv[3]);
    }
    return Usage();
}
