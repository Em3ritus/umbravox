// Force-included into every UI translation unit of test_trig: sinf/cosf are
// routed through functions that record the largest argument they are given.
//
// Why: the C5 has no FPU, so sinf/cosf are software, and newlib's fast path
// covers only |x| <= 2^7*(pi/2) ~= 201 rad. Beyond that every call takes the
// slow Payne-Hanek reduction. An animation phase fed straight from the growing
// frame counter crosses 201 rad after a minute or so and the draw time doubles.
#pragma once
#include <math.h>
#include <cmath>
float audit_maxArg();
void  audit_reset();
float audit_sinf(float);
float audit_cosf(float);
#define sinf audit_sinf
#define cosf audit_cosf
