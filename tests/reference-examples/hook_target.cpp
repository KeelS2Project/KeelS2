#include "hook_target.h"
namespace docs
{
int Calculator::Calculate(int value) { return value * 2; }
int InvokeCalculator(Calculator* calculator, int value) { return calculator->Calculate(value); }
}
