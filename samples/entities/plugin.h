#ifndef KEELS2_SAMPLE_ENTITIES_PLUGIN_H
#define KEELS2_SAMPLE_ENTITIES_PLUGIN_H

#include <keels2/keels2.hpp>

class EntitiesPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 Entities Example",
        "KeelS2 Project",
        "0.6.0",
        "Reads a typed field through a validated entity handle"
    };

    bool Load() override;
    void OnLevelShutdown() override;

private:
    void ReadWorldHealth(const CCommandContext&, const CCommand&);

    keels2::SchemaField<int32> health_;
    keels2::Entity world_;
};

#endif
