#pragma once

#define STATIC_ASSERT(expr) typedef char _sa_[(expr) ? 1 : -1]

#define CLAMP(x, lo, hi)  ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
