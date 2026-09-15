#include <keels2/authoring.hpp>
#include <keels2/services.hpp>
#include "math_service.h"

namespace docs
{
using namespace keels2::authoring;

class MathConsumer final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Math Consumer", "KeelS2 documentation", "1.0.0",
        "Query a versioned service and release its lease"};

    bool Load() override
    {
        if (services.Connect(HostContext()) != KEEL_RESULT_OK) return false;
        const void* value = nullptr;
        if (HostContext().QueryService(DOCS_MATH_NAME, DOCS_MATH_VERSION, &value) != KEEL_RESULT_OK) return false;
        math = static_cast<const DocsMathService*>(value);
        if (!math || math->size != sizeof(*math) || math->version != DOCS_MATH_VERSION || !math->add) return false;
        LogMessage("Math service: 20 + 22 = {}", math->add(20, 22));
        return CreateCommand("keel_docs_math_release", "Release the math service lease", &MathConsumer::Release);
    }

private:
    void Release(const CCommandContext&, const CCommand&)
    {
        if (!math) { LogMessage("Math lease is already released."); return; }
        const auto result = services.Release(DOCS_MATH_NAME, DOCS_MATH_VERSION);
        if (result == KEEL_RESULT_OK) { math = nullptr; LogMessage("Math lease released."); }
        else LogWarning("Lease release failed: {}", result);
    }

    keels2::services::Service services;
    const DocsMathService* math = nullptr;
};
}

KEELS2_PLUGIN(docs::MathConsumer)
