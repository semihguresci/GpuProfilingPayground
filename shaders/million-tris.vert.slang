struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    const uint gridWidth = 1000;
    const uint cellX = instanceID % gridWidth;
    const uint cellY = instanceID / gridWidth;

    const float2 gridSize = float2(1000.0, 1000.0);
    const float2 cellMin = (float2(cellX, cellY) / gridSize) * 2.0 - 1.0;
    const float2 cellSize = 2.0 / gridSize;

    float2 offsets[3] = {
        float2(0.10, 0.10),
        float2(0.90, 0.10),
        float2(0.50, 0.90)
    };

    VSOutput output;
    output.position = float4(cellMin + offsets[vertexID] * cellSize, 0.0, 1.0);
    output.color = float3(
        float(cellX & 31u) / 31.0,
        float(cellY & 31u) / 31.0,
        float((cellX ^ cellY) & 31u) / 31.0);
    return output;
}
