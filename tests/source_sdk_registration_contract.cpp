#include <keels2/source2_sdk.hpp>

CConVar<int> keels2_source_sdk_contract_value(
    "keels2_source_sdk_contract_value",
    FCVAR_NONE,
    "KeelS2 Source SDK registration contract",
    42);

CON_COMMAND_F(
    keels2_source_sdk_contract_command,
    "KeelS2 Source SDK command contract",
    FCVAR_NONE)
{
    (void)context;
    (void)args;
}
