#include <keels2/authoring.hpp>

using namespace keels2::authoring;

class Example : public Plugin
{
    bool Wrong(IGameEvent*);
    bool Load() override
    {
        return ListenForGameEvent("round_start", &Example::Wrong);
    }
};
