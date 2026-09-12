#include <keels2/authoring.hpp>

using namespace keels2::authoring;

class Example : public Plugin
{
    void Wrong(CPlayerSlot, const CCommand&);
    bool Load() override
    {
        return CreateCommand("example", "help", &Example::Wrong);
    }
};
