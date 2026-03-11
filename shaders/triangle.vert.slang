struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput main(uint vertexID : SV_VertexID)
{
    float2 positions[3] = {
        float2(0.0, -0.5),
        float2(0.5, 0.5),
        float2(-0.5, 0.5)
    };

    float3 colors[3] = {
        float3(1.0, 0.1, 0.1),
        float3(0.1, 1.0, 0.1),
        float3(0.1, 0.2, 1.0)
    };

    VSOutput o;
    o.position = float4(positions[vertexID], 0.0, 1.0);
    o.color = colors[vertexID];
    return o;
}
