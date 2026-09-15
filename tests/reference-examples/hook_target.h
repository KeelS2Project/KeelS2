#ifndef KEELS2_DOCS_HOOK_TARGET_H
#define KEELS2_DOCS_HOOK_TARGET_H
namespace docs
{
class Calculator
{
public:
    virtual int Calculate(int value);
};
int InvokeCalculator(Calculator* calculator, int value);
}
#endif
