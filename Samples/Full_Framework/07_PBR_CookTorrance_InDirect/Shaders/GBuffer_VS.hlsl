struct Mat
{
    matrix ModelMatrix;
    matrix ModelViewMatrix;
    matrix InverseTransposeModelMatrix;
    matrix ModelViewProjectionMatrix;
};

ConstantBuffer<Mat> MatCB : register(b0);

struct VertexPositionNormalTexture
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord : TEXCOORD;
};

struct GBufferVSOutput
{
    float3 NormalWS   : NORMAL;
    float2 TexCoord   : TEXCOORD;
    float4 Position   : SV_Position;
};

GBufferVSOutput main(VertexPositionNormalTexture IN)
{
    GBufferVSOutput OUT;

    OUT.Position = mul(MatCB.ModelViewProjectionMatrix, float4(IN.Position, 1.0f));
    OUT.NormalWS = mul((float3x3)MatCB.InverseTransposeModelMatrix, IN.Normal);
    OUT.TexCoord = IN.TexCoord;

    return OUT;
}