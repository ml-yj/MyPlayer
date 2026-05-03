
#pragma once

#include <vector>

struct VadChunk
{

    long long startMs = 0;

    std::vector<float> samples;

    bool flushAfter = false;
};
