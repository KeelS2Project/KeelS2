#include <keels2/authoring.hpp>
#include <keels2/services.hpp>
#include "math_service.h"
#include <algorithm>
#include <limits>

namespace docs
{
using namespace keels2::authoring;

class MathProvider final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Math Provider", "KeelS2 documentation", "1.0.0",
        "Publish a versioned service and respect consumer leases"};

    bool Load() override
    {
        return services.Connect(HostContext()) == KEEL_RESULT_OK
            && services.Publish(DOCS_MATH_NAME, DOCS_MATH_VERSION, &math, publication) == KEEL_RESULT_OK
            && CreateCommand("keel_docs_math_withdraw", "Withdraw the math publication", &MathProvider::Withdraw);
    }

private:
    static std::int32_t Add(std::int32_t left, std::int32_t right)
    {
        return static_cast<std::int32_t>(std::clamp<std::int64_t>(static_cast<std::int64_t>(left) + right,
            std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
    }

    void Withdraw(const CCommandContext&, const CCommand&)
    {
        if (!publication)
        {
            LogMessage("Publication is already withdrawn.");
            return;
        }

        const auto result = services.Withdraw(publication);

        if (result == KEEL_RESULT_OK)
        {
            publication = 0;
            LogMessage("Publication withdrawn.");
        }
        else if (result == KEEL_RESULT_BUSY) LogMessage("Release consumer leases before withdrawal.");
        else LogWarning("Withdrawal failed: {}", result);
    }

    keels2::services::Service services;
    KeelServiceHandle publication = 0;
    const DocsMathService math{sizeof(math), DOCS_MATH_VERSION, &Add};
};
}

KEELS2_PLUGIN(docs::MathProvider)
