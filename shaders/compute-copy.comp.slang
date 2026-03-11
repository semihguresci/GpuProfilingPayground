[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> outputBuffer : register(u0);

[[vk::push_constant]]
cbuffer PushConstants {
    uint frameSeed;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint value = dispatchThreadID.x ^ (frameSeed * 747796405u + 2891336453u);

    for (uint i = 0; i < 512u; ++i) {
        value = value * 1664525u + 1013904223u + i;
        value ^= value >> 16;
        value *= 2246822519u;
        value ^= value >> 13;
    }

    outputBuffer[dispatchThreadID.x] = value;
}
