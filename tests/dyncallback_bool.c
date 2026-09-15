#include <dyncall_args_x64.h>

int KeelTest_DyncallbackBool(void)
{
    int64 stack_value = 0x100;
    DCArgs stack_false = {0};
    stack_false.stack_ptr = &stack_value;
    stack_false.reg_count.i = numIntRegs;
    stack_false.aggr_return_register = -1;
    if (dcbArgBool(&stack_false) != DC_FALSE)
    {
        return 0;
    }

    stack_value = 0x101;
    DCArgs stack_true = {0};
    stack_true.stack_ptr = &stack_value;
    stack_true.reg_count.i = numIntRegs;
    stack_true.aggr_return_register = -1;
    if (dcbArgBool(&stack_true) != DC_TRUE)
    {
        return 0;
    }

    int64 unused = 0;
    DCArgs register_false = {0};
    register_false.stack_ptr = &unused;
    register_false.reg_count.i = 0;
    register_false.aggr_return_register = -1;
    register_false.reg_data.i[0] = 0x100;
    if (dcbArgBool(&register_false) != DC_FALSE)
    {
        return 0;
    }

    DCArgs register_true = {0};
    register_true.stack_ptr = &unused;
    register_true.reg_count.i = 0;
    register_true.aggr_return_register = -1;
    register_true.reg_data.i[0] = 0x101;
    return dcbArgBool(&register_true) == DC_TRUE;
}
