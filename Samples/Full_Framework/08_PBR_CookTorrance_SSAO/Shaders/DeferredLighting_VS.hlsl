struct FullScreenVSOutput
{
    float2 TexCoord : TEXCOORD;
    float4 Position : SV_Position;
};

// Full-screen triangle (no vertex buffer needed)
FullScreenVSOutput main(uint vertexID : SV_VertexID)
{
    FullScreenVSOutput output;
    
    // Generate full-screen triangle
    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    
    return output;
}