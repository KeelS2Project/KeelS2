#ifndef KEELS2_TEST_PUBLISHED_SERVICE_FIXTURE_H
#define KEELS2_TEST_PUBLISHED_SERVICE_FIXTURE_H

#include <stdint.h>

#define KEELS2_TEST_MATH_SERVICE_NAME "fixture.math"
#define KEELS2_TEST_MATH_SERVICE_VERSION 1u

typedef struct KeelTestMathService
{
    uint32_t size;
    uint32_t version;
    int32_t (*add)(int32_t left, int32_t right);
} KeelTestMathService;

#endif
