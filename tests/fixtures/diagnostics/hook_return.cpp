#include <keels2/authoring.hpp>

using namespace keels2::authoring;

class Example : public Plugin
{
    bool Wrong(ConCommandRef, const CCommandContext&, const CCommand&);
    bool Load() override
    {
        return HookPre(GetCVarSystem<ICvar>(), &ICvar::DispatchConCommand, &Example::Wrong);
    }
};
