#ifndef KEELS2_DOCS_MATH_SERVICE_H
#define KEELS2_DOCS_MATH_SERVICE_H
#include <stdint.h>
#define DOCS_MATH_NAME "docs.math"
#define DOCS_MATH_VERSION 1u
typedef struct DocsMathService
{
    uint32_t size;
    uint32_t version;
    int32_t (*add)(int32_t left, int32_t right);
} DocsMathService;
#endif
