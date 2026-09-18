#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#define IMPORT __declspec(dllimport)
#else
#define EXPORT __attribute__((visibility("default")))
#define IMPORT
#endif
extern "C" IMPORT int KeelDependencyValue();
extern "C" EXPORT int KeelDependencyConsumer() { return KeelDependencyValue() + 1; }
