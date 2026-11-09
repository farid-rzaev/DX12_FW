// Shared VS of every full-screen pass (lighting, SSAO, blur, AO debug copy, tonemap).
// Drawn with PipelineStage::DrawFullscreenTriangle -> Draw(3), no vertex buffer.
//
// One triangle twice the size of the screen instead of a two-triangle quad: no diagonal seam,
// so no 2x2 pixel quads shaded twice along it.
//
//   vertexID   TexCoord   Position (NDC)
//      0        (0, 0)     (-1,  1)   top-left
//      1        (2, 0)     ( 3,  1)   far right
//      2        (0, 2)     (-1, -3)   far bottom
//
// Clockwise in screen space = front-facing with the default rasterizer state. TexCoord is
// linear in Position, so it is exactly [0, 1] across the visible part (V down, matching textures).

struct FullScreenVSOutput
{
    float2 TexCoord : TEXCOORD;
    float4 Position : SV_Position;
};

FullScreenVSOutput main(uint vertexID : SV_VertexID)
{
    FullScreenVSOutput output;

    output.TexCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(output.TexCoord * float2(2, -2) + float2(-1, 1), 0, 1);

    return output;
}
