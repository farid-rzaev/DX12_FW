#include "Sample4.h"

#include <Framework/Application.h>
#include <Framework/CommandQueue.h>
#include <Framework/CommandList.h>

#include <Framework/Gameplay/Light.h>
#include <Framework/Material/Material.h>

#include <Framework/3RD_Party/Helpers.h>

#include <wrl.h>
using namespace Microsoft::WRL;

#include <Framework/3RD_Party/D3D/d3dx12.h>
#include <d3dcompiler.h>
#include <DirectXColors.h>
#include <DirectXMath.h>

using namespace DirectX;

#include <algorithm> // For std::min and std::max.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif


// =====================================================================================
//								        Structs
// =====================================================================================


struct Mat
{
    XMMATRIX ModelMatrix;
    XMMATRIX ModelViewMatrix;
    XMMATRIX InverseTransposeModelViewMatrix;
    XMMATRIX ModelViewProjectionMatrix;
};

struct LightProperties
{
    uint32_t NumPointLights;
    uint32_t NumSpotLights;
};

enum TonemapMethod : uint32_t
{
    TM_Linear,
    TM_Reinhard,
    TM_ReinhardSq,
    TM_ACESFilmic,
};

struct TonemapParameters
{
    TonemapParameters()
        : TonemapMethod(TM_Reinhard)
        , Exposure(0.0f)
        , MaxLuminance(1.0f)
        , K(1.0f)
        , A(0.22f)
        , B(0.3f)
        , C(0.1f)
        , D(0.2f)
        , E(0.01f)
        , F(0.3f)
        , LinearWhite(11.2f)
        , Gamma(2.2f)
    {}

    // The method to use to perform tonemapping.
    TonemapMethod TonemapMethod;
    // Exposure should be expressed as a relative exposure value (-2, -1, 0, +1, +2 )
    float Exposure;

    // The maximum luminance to use for linear tonemapping.
    float MaxLuminance;

    // Reinhard constant. Generally this is 1.0.
    float K;

    // ACES Filmic parameters
    // See: https://www.slideshare.net/ozlael/hable-john-uncharted2-hdr-lighting/142
    float A; // Shoulder strength
    float B; // Linear strength
    float C; // Linear angle
    float D; // Toe strength
    float E; // Toe Numerator
    float F; // Toe denominator
    // Note E/F = Toe angle.
    float LinearWhite;
    float Gamma;
};

// An enum for root signature parameters.
// I'm not using scoped enums to avoid the explicit cast that would be required
// to use these as root indices in the root signature.
enum RootParameters
{
    MatricesCB,         // ConstantBuffer<Mat> MatCB : register(b0);                            <- vs
    MaterialCB,         // ConstantBuffer<Material> MaterialCB : register( b0, space1 );        <- ps
    LightPropertiesCB,  // ConstantBuffer<LightProperties> LightPropertiesCB : register( b1 );  <- ps
    PointLights,        // StructuredBuffer<PointLight> PointLights : register( t0 );           <- ps
    SpotLights,         // StructuredBuffer<SpotLight> SpotLights : register( t1 );             <- ps
    Textures,           // Texture2D DiffuseTexture : register( t2 );                           <- ps
    NumRootParameters
};

enum GbufferRootParams
{
    MatricesCB_GBuffer,         // ConstantBuffer<Mat> MatCB : register(b0);                            <- vs
    MaterialCB_GBuffer,         // ConstantBuffer<Material> MaterialCB : register( b0, space1 );        <- ps
    Textures_GBuffer,           // Texture2D DiffuseTexture : register( t0 );                           <- ps
    NumRootParameters_Gbuffer
};

enum DeferredRootParams
{
    MaterialCB_Deferred,             // b0, space1                                                   <- vs
    LightPropertiesCB_Deferred,      // b1                                                           <- ps
    PointLights_Deferred,            // t0                                                           <- ps
    SpotLights_Deferred,             // t1                                                           <- ps
    Textures_Deferred,               // t2-t4: G-Buffer textures                                     <- ps
    NumRootParameters_Deferred
};


// =====================================================================================
//								        Globals
// =====================================================================================


TonemapParameters g_TonemapParameters;


// =====================================================================================
//								      Helper Funcs
// =====================================================================================


// Clamp a value between a min and max range.
template<typename T>
constexpr const T& clamp(const T& val, const T& min = T(0), const T& max = T(1))
{
    return val < min ? min : val > max ? max : val;
}

// Builds a look-at (world) matrix from a point, up and direction vectors.
XMMATRIX XM_CALLCONV LookAtMatrix(FXMVECTOR Position, FXMVECTOR Direction, FXMVECTOR Up)
{
    assert(!XMVector3Equal(Direction, XMVectorZero()));
    assert(!XMVector3IsInfinite(Direction));
    assert(!XMVector3Equal(Up, XMVectorZero()));
    assert(!XMVector3IsInfinite(Up));

    XMVECTOR R2 = XMVector3Normalize(Direction);

    XMVECTOR R0 = XMVector3Cross(Up, R2);
    R0 = XMVector3Normalize(R0);

    XMVECTOR R1 = XMVector3Cross(R2, R0);

    XMMATRIX M(R0, R1, R2, Position);

    return M;
}


// =====================================================================================
//								        Sample
// =====================================================================================


Sample4::Sample4()
    : Game()
    , m_ScissorRect(CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX))
    , m_Forward(0)
    , m_Backward(0)
    , m_Left(0)
    , m_Right(0)
    , m_Up(0)
    , m_Down(0)
    , m_Pitch(0)
    , m_Yaw(0)
    , m_AnimateLights(false)
    , m_Shift(false)
    , m_Width(0)
    , m_Height(0)
    , m_RenderScale(1.0f)
{
    m_pAlignedCameraData = (CameraData*)_aligned_malloc(sizeof(CameraData), 16);
    if (m_pAlignedCameraData)
    {
        m_pAlignedCameraData->m_InitialCamPos = m_Camera.get_Translation();
        m_pAlignedCameraData->m_InitialCamRot = m_Camera.get_Rotation();
        m_pAlignedCameraData->m_InitialFov = m_Camera.get_FoV();
    }
}

Sample4::~Sample4()
{
    _aligned_free(m_pAlignedCameraData);
}

bool Sample4::Initialize(const wchar_t* windowTitle, int width, int height, bool vSync)
{
    if (!Game::Initialize(windowTitle, width, height, vSync)) return false;

    ResizeEventArgs resizeEventArgs(width, height);
    OnResize(resizeEventArgs);


    XMVECTOR cameraPos = XMVectorSet(10, 10, -20, 1);
    XMVECTOR cameraTarget = XMVectorSet(0, 5, 0, 1);
    XMVECTOR cameraUp = XMVectorSet(0, 1, 0, 0);

    m_Camera.set_LookAt(cameraPos, cameraTarget, cameraUp);
    m_Camera.set_Projection(45.0f, width / (float)height, 0.1f, 100.0f);

    return true;
}

bool Sample4::LoadContent()
{
    auto& app = Application::Get();
    auto  device        = app.GetDevice();
    auto  commandQueue  = app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    auto  commandList   = commandQueue->GetCommandList();

    // Set solution dir as current working dirrectory
    std::wstring exe_path_str = GetExePath();
    ThrowIfFailed(exe_path_str.empty() == false, "Can't find the .exe path!");
    // --
    SetWorkingDirToSolutionDir(exe_path_str);
	std::wstring solution_dir_str = exe_path_str;

    // Load some textures
    commandList->LoadTextureFromFile(m_DefaultTexture, L"Assets/Textures/DefaultWhite.bmp");
    commandList->LoadTextureFromFile(m_DirectXTexture, L"Assets/Textures/Directx9.png");
    commandList->LoadTextureFromFile(m_GraceCathedralTexture, L"Assets/Textures/grace-new.hdr");

    // Create Meshes
    m_SphereMesh = Mesh::CreateSphere(*commandList);
    m_ConeMesh = Mesh::CreateCone(*commandList);

    // Create an inverted (reverse winding order) cube so the insides are not clipped.
    m_SkyboxMesh = Mesh::CreateCube(*commandList, 1.0f, true);

    m_LoadedMeshParts = AssimpLoader::Load(*commandList, L"Assets/Models/FBX/Sponza/Sponza.fbx", m_DefaultTexture);
    //m_LoadedMeshParts = AssimpLoader::Load(*commandList, L"Assets/Models/FBX/sponza_2/sponza.sdkmesh", m_DefaultTexture);
    
	// [OPT_2] Sort the loaded mesh parts by their diffuse texture to minimize texture binding changes when rendering.
    std::sort(m_LoadedMeshParts.begin(), m_LoadedMeshParts.end(),
        [](const LoadedMeshPart& a, const LoadedMeshPart& b) {
            return &a.diffuseTexture < &b.diffuseTexture;
        });

    // Create a cubemap for the HDR panorama.
    auto cubemapDesc = m_GraceCathedralTexture.GetD3D12ResourceDesc();
    cubemapDesc.Width = cubemapDesc.Height = 1024;
    cubemapDesc.DepthOrArraySize = 6;
    cubemapDesc.MipLevels = 0;

    m_GraceCathedralCubemap = Texture(cubemapDesc, nullptr, TextureUsage::Albedo, L"Grace Cathedral Cubemap");
    // Convert the 2D panorama to a 3D cubemap.
    commandList->PanoToCubemap(m_GraceCathedralCubemap, m_GraceCathedralTexture);

    // Create an HDR intermediate render target.
    DXGI_FORMAT HDRFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT;

    // Create an off-screen render target with a single color buffer and a depth buffer.
    auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D(HDRFormat, m_Width, m_Height);
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE colorClearValue;
    colorClearValue.Format = colorDesc.Format;
    colorClearValue.Color[0] = 0.4f;
    colorClearValue.Color[1] = 0.6f;
    colorClearValue.Color[2] = 0.9f;
    colorClearValue.Color[3] = 1.0f;

    Texture HDRTexture = Texture(colorDesc, &colorClearValue,
        TextureUsage::RenderTarget,
        L"HDR Texture");

    // Create a depth buffer for the HDR render target.
    auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(depthBufferFormat, m_Width, m_Height);
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClearValue;
    depthClearValue.Format = depthDesc.Format;
    depthClearValue.DepthStencil = { 1.0f, 0 };

    Texture depthTexture = Texture(depthDesc, &depthClearValue,
        TextureUsage::Depth,
        L"Depth Render Target");

    // Attach the HDR texture to the HDR render target.
    m_HDRRenderTarget.AttachTexture(AttachmentPoint::Color0, HDRTexture);
    m_HDRRenderTarget.AttachTexture(AttachmentPoint::DepthStencil, depthTexture);

    // Create G-Buffer render target with multiple color attachments
    {
        DXGI_FORMAT gbufferFormats[NUM_GBUFFER_RTS] = {
            DXGI_FORMAT_R8G8B8A8_UNORM,         // Albedo + Specular Power
            DXGI_FORMAT_R16G16B16A16_FLOAT,     // Normal (higher precision)
            DXGI_FORMAT_R32G32B32A32_FLOAT      // Position (view space)
        };

        auto colorDesc = CD3DX12_RESOURCE_DESC::Tex2D(gbufferFormats[0], m_Width, m_Height);
        colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        // --
        colorClearValue.Format = gbufferFormats[0];
        // --
        Texture gbufferRT0 = Texture(colorDesc, &colorClearValue, TextureUsage::RenderTarget, L"GBuffer RT0");
    
		colorDesc.Format = gbufferFormats[1];
        colorClearValue.Format = gbufferFormats[1];
        Texture gbufferRT1 = Texture(colorDesc, &colorClearValue, TextureUsage::RenderTarget, L"GBuffer RT1");

		colorDesc.Format = gbufferFormats[2];
        colorClearValue.Format = gbufferFormats[2];
        Texture gbufferRT2 = Texture(colorDesc, &colorClearValue, TextureUsage::RenderTarget, L"GBuffer RT2");


        // Create G-Buffer with multiple render targets
        m_GBufferRT.AttachTexture(AttachmentPoint::Color0, gbufferRT0);
        m_GBufferRT.AttachTexture(AttachmentPoint::Color1, gbufferRT1);
        m_GBufferRT.AttachTexture(AttachmentPoint::Color2, gbufferRT2);
        m_GBufferRT.AttachTexture(AttachmentPoint::DepthStencil, depthTexture);

		colorDesc.Format = HDRFormat;
        colorClearValue.Format = HDRFormat;
        Texture deferredLightingRT = Texture(colorDesc, &colorClearValue, TextureUsage::RenderTarget, L"DeferredLighting RT");

        // Create deferred lighting output buffer (HDR, before tone mapping)
        m_DeferredLightingRT.AttachTexture(AttachmentPoint::Color0, deferredLightingRT);
    }


    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Create a root signature and PSO for the skybox shaders.
    {
        // Load the Skybox shaders.
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        ThrowIfFailed(D3DReadFileToBlob( (solution_dir_str + L"\\Skybox_VS.cso").c_str(), &vs));
        ThrowIfFailed(D3DReadFileToBlob( (solution_dir_str + L"\\Skybox_PS.cso").c_str(), &ps));

        // Setup the input layout for the skybox vertex shader.
        D3D12_INPUT_ELEMENT_DESC inputLayout[1] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Allow input layout and deny unnecessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[0].InitAsConstants(sizeof(DirectX::XMMATRIX) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        rootParameters[1].InitAsDescriptorTable(1, &descriptorRange, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_STATIC_SAMPLER_DESC linearClampSampler(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1(2, rootParameters, 1, &linearClampSampler, rootSignatureFlags);

        m_SkyboxSignature.SetRootSignatureDesc(rootSignatureDescription.Desc_1_1, featureData.HighestVersion);

        // Setup the Skybox pipeline state.
        struct SkyboxPipelineState
        {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
            CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
            CD3DX12_PIPELINE_STATE_STREAM_VS VS;
            CD3DX12_PIPELINE_STATE_STREAM_PS PS;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        } skyboxPipelineStateStream;

        skyboxPipelineStateStream.pRootSignature = m_SkyboxSignature.GetRootSignature().Get();
        skyboxPipelineStateStream.InputLayout = { inputLayout, 1 };
        skyboxPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        skyboxPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
        skyboxPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
        skyboxPipelineStateStream.RTVFormats = m_HDRRenderTarget.GetRenderTargetFormats();

        D3D12_PIPELINE_STATE_STREAM_DESC skyboxPipelineStateStreamDesc = {
            sizeof(SkyboxPipelineState), &skyboxPipelineStateStream
        };
        ThrowIfFailed(device->CreatePipelineState(&skyboxPipelineStateStreamDesc, IID_PPV_ARGS(&m_SkyboxPipelineState)));
    }

    // Create a root signature for the HDR pipeline.
    {
        // Load the HDR shaders.
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        ThrowIfFailed(D3DReadFileToBlob((solution_dir_str + L"\\HDR_VS.cso").c_str(), &vs));
        ThrowIfFailed(D3DReadFileToBlob((solution_dir_str + L"\\HDR_PS.cso").c_str(), &ps));

        // Allow input layout and deny unnecessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
        // --
        CD3DX12_ROOT_PARAMETER1 rootParameters[RootParameters::NumRootParameters];
        rootParameters[RootParameters::MatricesCB].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);  // b0, VERTEX shader uses SPACE0
        rootParameters[RootParameters::MaterialCB].InitAsConstantBufferView(0, 1, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);   // b0, PIXEL  shader uses SPACE1
        rootParameters[RootParameters::LightPropertiesCB].InitAsConstants(sizeof(LightProperties) / 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);         // b1
        rootParameters[RootParameters::PointLights].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);  // t0
        rootParameters[RootParameters::SpotLights].InitAsShaderResourceView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);   // t1
        rootParameters[RootParameters::Textures].InitAsDescriptorTable(1, &descriptorRange, D3D12_SHADER_VISIBILITY_PIXEL);                          // t2 - DESCRIPTOR HEAP see descriptorRange
        // --
        CD3DX12_STATIC_SAMPLER_DESC linearRepeatSampler(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR);                                              // s0

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1(RootParameters::NumRootParameters, rootParameters, 1, &linearRepeatSampler, rootSignatureFlags);

        m_HDRRootSignature.SetRootSignatureDesc(rootSignatureDescription.Desc_1_1, featureData.HighestVersion);

        // Setup the HDR pipeline state.
        struct HDRPipelineStateStream
        {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
            CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
            CD3DX12_PIPELINE_STATE_STREAM_VS VS;
            CD3DX12_PIPELINE_STATE_STREAM_PS PS;
            CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        } hdrPipelineStateStream;

        hdrPipelineStateStream.pRootSignature = m_HDRRootSignature.GetRootSignature().Get();
        hdrPipelineStateStream.InputLayout = { VertexPositionNormalTexture::InputElements, VertexPositionNormalTexture::InputElementCount };
        hdrPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        hdrPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
        hdrPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
        hdrPipelineStateStream.DSVFormat = m_HDRRenderTarget.GetDepthStencilFormat();
        hdrPipelineStateStream.RTVFormats = m_HDRRenderTarget.GetRenderTargetFormats();

        D3D12_PIPELINE_STATE_STREAM_DESC hdrPipelineStateStreamDesc = {
            sizeof(HDRPipelineStateStream), &hdrPipelineStateStream
        };
        ThrowIfFailed(device->CreatePipelineState(&hdrPipelineStateStreamDesc, IID_PPV_ARGS(&m_HDRPipelineState)));
    }

    // Create the SDR Root Signature
    {
        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[0].InitAsConstants(sizeof(TonemapParameters) / 4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[1].InitAsDescriptorTable(1, &descriptorRange, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_STATIC_SAMPLER_DESC linearClampsSampler(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1(2, rootParameters, 1, &linearClampsSampler );

        m_SDRRootSignature.SetRootSignatureDesc(rootSignatureDescription.Desc_1_1, featureData.HighestVersion);

        // Create the SDR PSO
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        ThrowIfFailed(D3DReadFileToBlob((solution_dir_str + L"\\HDRtoSDR_VS.cso").c_str(), &vs));
        ThrowIfFailed(D3DReadFileToBlob((solution_dir_str + L"\\HDRtoSDR_PS.cso").c_str(), &ps));

        CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
        rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;

        struct SDRPipelineStateStream
        {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
            CD3DX12_PIPELINE_STATE_STREAM_VS VS;
            CD3DX12_PIPELINE_STATE_STREAM_PS PS;
            CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER Rasterizer;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        } sdrPipelineStateStream;

        sdrPipelineStateStream.pRootSignature = m_SDRRootSignature.GetRootSignature().Get();
        sdrPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        sdrPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
        sdrPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
        sdrPipelineStateStream.Rasterizer = rasterizerDesc;
        sdrPipelineStateStream.RTVFormats = app.GetRenderTarget().GetRenderTargetFormats();

        D3D12_PIPELINE_STATE_STREAM_DESC sdrPipelineStateStreamDesc = {
            sizeof(SDRPipelineStateStream), &sdrPipelineStateStream
        };
        ThrowIfFailed(device->CreatePipelineState(&sdrPipelineStateStreamDesc, IID_PPV_ARGS(&m_SDRPipelineState)));
    }

	// [G-Buffer] - Root Signature and PSO
    {
        // === G-Buffer Root Signature (same as HDR forward) ===

        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        // --
        CD3DX12_ROOT_PARAMETER1 rootParameters[GbufferRootParams::NumRootParameters_Gbuffer];
        rootParameters[GbufferRootParams::MatricesCB_GBuffer].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_VERTEX);  // b0, VERTEX shader uses SPACE0
        rootParameters[GbufferRootParams::MaterialCB_GBuffer].InitAsConstantBufferView(0, 1, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);   // b0, PIXEL  shader uses SPACE1
        rootParameters[GbufferRootParams::Textures_GBuffer].InitAsDescriptorTable(1, &descriptorRange, D3D12_SHADER_VISIBILITY_PIXEL);                          // t0 - DESCRIPTOR HEAP see descriptorRange
        

        CD3DX12_STATIC_SAMPLER_DESC linearRepeatSampler(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR);                                                         // s0 

        // Allow input layout and deny unnecessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1(GbufferRootParams::NumRootParameters_Gbuffer, rootParameters, 1, &linearRepeatSampler, rootSignatureFlags);

        m_GBufferRootSignature.SetRootSignatureDesc(rootSignatureDescription.Desc_1_1, featureData.HighestVersion);

        // === G-Buffer PSO ===

        // G-Buffer shaders
        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        ThrowIfFailed(D3DReadFileToBlob((solution_dir_str + L"\\GBuffer_VS.cso").c_str(), &vs));
        ThrowIfFailed(D3DReadFileToBlob((solution_dir_str + L"\\GBuffer_PS.cso").c_str(), &ps));

        struct PipelineStateStream
        {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
            CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
            CD3DX12_PIPELINE_STATE_STREAM_VS VS;
            CD3DX12_PIPELINE_STATE_STREAM_PS PS;
            CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        } gbufferPipelineStateStream;

        gbufferPipelineStateStream.pRootSignature = m_GBufferRootSignature.GetRootSignature().Get();
        gbufferPipelineStateStream.InputLayout = { VertexPositionNormalTexture::InputElements, VertexPositionNormalTexture::InputElementCount };
        gbufferPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        gbufferPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
        gbufferPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
        gbufferPipelineStateStream.DSVFormat  = m_GBufferRT.GetDepthStencilFormat();
        gbufferPipelineStateStream.RTVFormats = m_GBufferRT.GetRenderTargetFormats();

        D3D12_PIPELINE_STATE_STREAM_DESC gbufferPipelineStateStreamDesc = {
            sizeof(PipelineStateStream), &gbufferPipelineStateStream
        };
        ThrowIfFailed(device->CreatePipelineState(&gbufferPipelineStateStreamDesc, IID_PPV_ARGS(&m_GBufferPSO)));
    }

    // [Deferred Lighting] - Root Signature and PSO
    {
        // === Deferred Lighting Root Signature ===

        CD3DX12_DESCRIPTOR_RANGE1 descriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 2);
        // --
        CD3DX12_ROOT_PARAMETER1 rootParameters[DeferredRootParams::NumRootParameters_Deferred];
        rootParameters[DeferredRootParams::MaterialCB_Deferred].InitAsConstantBufferView(0, 1, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);   // b0, PIXEL  shader uses SPACE1
        rootParameters[DeferredRootParams::LightPropertiesCB_Deferred].InitAsConstants(sizeof(LightProperties) / 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);         // b1
        rootParameters[DeferredRootParams::PointLights_Deferred].InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);  // t0
        rootParameters[DeferredRootParams::SpotLights_Deferred].InitAsShaderResourceView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);   // t1
        rootParameters[DeferredRootParams::Textures_Deferred].InitAsDescriptorTable(1, &descriptorRange, D3D12_SHADER_VISIBILITY_PIXEL);                          // t2,t3,t4 - DESCRIPTOR HEAP see descriptorRange


        D3D12_STATIC_SAMPLER_DESC pointSampler = {};
        pointSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        pointSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        pointSampler.ShaderRegister = 0;
        pointSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        // --
        CD3DX12_STATIC_SAMPLER_DESC linearRepeatSampler(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR);                                                  // s0


        // Allow input layout and deny unnecessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
        rootSignatureDescription.Init_1_1(DeferredRootParams::NumRootParameters_Deferred, rootParameters, 1, &pointSampler, rootSignatureFlags);

        m_DeferredLightingRootSignature.SetRootSignatureDesc(rootSignatureDescription.Desc_1_1, featureData.HighestVersion);

        // === Deferred Lighting PSO ===

        struct DeferredPipelineStateStream
        {
            CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
            CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
            CD3DX12_PIPELINE_STATE_STREAM_VS VS;
            CD3DX12_PIPELINE_STATE_STREAM_PS PS;
            CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL1 DepthStencil;
            CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
        } deferredPipelineStateStream;

        ComPtr<ID3DBlob> vs, ps;
        ThrowIfFailed(D3DReadFileToBlob((solution_dir_str + L"DeferredLighting_VS.cso").c_str(), &vs));
        ThrowIfFailed(D3DReadFileToBlob((solution_dir_str + L"DeferredLighting_PS.cso").c_str(), &ps));

        deferredPipelineStateStream.pRootSignature = m_DeferredLightingRootSignature.GetRootSignature().Get();
        deferredPipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        deferredPipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
        deferredPipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(ps.Get());

        CD3DX12_DEPTH_STENCIL_DESC1 depthStencilDesc(D3D12_DEFAULT);
        depthStencilDesc.DepthEnable = FALSE;                                                    // No depth test for full-screen quad
        // --
        deferredPipelineStateStream.DepthStencil = depthStencilDesc;
        deferredPipelineStateStream.RTVFormats   = m_HDRRenderTarget.GetRenderTargetFormats();   // HDR output

        D3D12_PIPELINE_STATE_STREAM_DESC deferredPipelineStateStreamDesc = {
            sizeof(DeferredPipelineStateStream), &deferredPipelineStateStream
        };
        ThrowIfFailed(device->CreatePipelineState(&deferredPipelineStateStreamDesc, IID_PPV_ARGS(&m_DeferredLightingPSO)));
    }

    auto fenceValue = commandQueue->ExecuteCommandList(commandList);
    commandQueue->WaitForFenceValue(fenceValue);

    return true;
}

void Sample4::RescaleHDRRenderTarget(float scale)
{
    uint32_t width = static_cast<uint32_t>(m_Width * scale);
    uint32_t height = static_cast<uint32_t>(m_Height * scale);
    
    width = clamp<uint32_t>(width, 1, D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION);
    height = clamp<uint32_t>(height, 1, D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION);

    m_HDRRenderTarget.Resize(width, height);
}

void Sample4::OnResize(ResizeEventArgs& e)
{
    Game::OnResize(e);

    if (m_Width != e.Width || m_Height != e.Height)
    {
        m_Width = std::max(1, e.Width);
        m_Height = std::max(1, e.Height);

        float fov = m_Camera.get_FoV();
        float aspectRatio = m_Width / (float)m_Height;
        m_Camera.set_Projection(fov, aspectRatio, 0.1f, 200.0f);

        RescaleHDRRenderTarget(m_RenderScale);
    }
}

void Sample4::UnloadContent()
{
}

static double g_FPS = 0.0;

void Sample4::OnUpdate()
{
    static uint64_t frameCount = 0;
    static double totalTime = 0.0;

    Game::OnUpdate();

    double elapsedTime = Game::GetUpdateDeltaSeconds();

    totalTime += elapsedTime;
    frameCount++;

    if (totalTime > 1.0)
    {
        g_FPS = frameCount / totalTime;

        char buffer[512];
        sprintf_s(buffer, "FPS: %f\n", g_FPS);
        OutputDebugStringA(buffer);

        frameCount = 0;
        totalTime = 0.0;
    }

    // Update the camera.
    float speedMultipler = (m_Shift ? 16.0f : 4.0f);

    XMVECTOR cameraTranslate = XMVectorSet(m_Right - m_Left, 0.0f, m_Forward - m_Backward, 1.0f) * speedMultipler * static_cast<float>(elapsedTime);
    XMVECTOR cameraPan = XMVectorSet(0.0f, m_Up - m_Down, 0.0f, 1.0f) * speedMultipler * static_cast<float>(elapsedTime);
    m_Camera.Translate(cameraTranslate, Space::Local);
    m_Camera.Translate(cameraPan, Space::Local);

    XMVECTOR cameraRotation = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(m_Pitch), XMConvertToRadians(m_Yaw), 0.0f);
    m_Camera.set_Rotation(cameraRotation);

    XMMATRIX viewMatrix = m_Camera.get_ViewMatrix();

    const int numPointLights = 4;
    const int numSpotLights = 4;

    static const XMVECTORF32 LightColors[] =
    {
        Colors::White, Colors::Orange, Colors::Yellow, Colors::Green, Colors::Blue, Colors::Indigo, Colors::Violet, Colors::White
    };

    static float lightAnimTime = 0.0f;
    if (m_AnimateLights)
    {
        lightAnimTime += static_cast<float>(elapsedTime) * 0.5f * XM_PI;
    }

    const float radius = 8.0f;
    const float offset = 2.0f * XM_PI / numPointLights;
    const float offset2 = offset + (offset / 2.0f);

    // Setup the light buffers.
    m_PointLights.resize(numPointLights);
    for (int i = 0; i < numPointLights; ++i)
    {
        PointLight& l = m_PointLights[i];

        l.PositionWS = {
            static_cast<float>(std::sin(lightAnimTime + offset * i)) * radius,
            9.0f,
            static_cast<float>(std::cos(lightAnimTime + offset * i)) * radius,
            1.0f
        };
        XMVECTOR positionWS = XMLoadFloat4(&l.PositionWS);
        XMVECTOR positionVS = XMVector3TransformCoord(positionWS, viewMatrix);
        XMStoreFloat4(&l.PositionVS, positionVS);

        l.Color = XMFLOAT4(LightColors[i]);
        l.Intensity = 1.0f;
        l.Attenuation = 0.0f;
    }

    m_SpotLights.resize(numSpotLights);
    for (int i = 0; i < numSpotLights; ++i)
    {
        SpotLight& l = m_SpotLights[i];

        l.PositionWS = {
            static_cast<float>(std::sin(lightAnimTime + offset * i + offset2)) * radius,
            9.0f,
            static_cast<float>(std::cos(lightAnimTime + offset * i + offset2)) * radius,
            1.0f
        };
        XMVECTOR positionWS = XMLoadFloat4(&l.PositionWS);
        XMVECTOR positionVS = XMVector3TransformCoord(positionWS, viewMatrix);
        XMStoreFloat4(&l.PositionVS, positionVS);

        XMVECTOR directionWS = XMVector3Normalize(XMVectorSetW(XMVectorNegate(positionWS), 0));
        XMVECTOR directionVS = XMVector3Normalize(XMVector3TransformNormal(directionWS, viewMatrix));
        XMStoreFloat4(&l.DirectionWS, directionWS);
        XMStoreFloat4(&l.DirectionVS, directionVS);

        l.Color = XMFLOAT4(LightColors[numPointLights + i]);
        l.Intensity = 1.0f;
        l.SpotAngle = XMConvertToRadians(45.0f);
        l.Attenuation = 0.0f;
    }
}

// Helper to display a little (?) mark which shows a tooltip when hovered.
static void ShowHelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Number of values to plot in the tonemapping curves.
static const int VALUES_COUNT = 256;
// Maximum HDR value to normalize the plot samples.
static const float HDR_MAX = 12.0f;

float LinearTonemapping(float HDR, float max)
{
    if (max > 0.0f)
    {
        return clamp(HDR / max);
    }
    return HDR;
}

float LinearTonemappingPlot(void*, int index)
{
    return LinearTonemapping(index / (float)VALUES_COUNT * HDR_MAX, g_TonemapParameters.MaxLuminance);
}

// Reinhard tone mapping.
// See: http://www.cs.utah.edu/~reinhard/cdrom/tonemap.pdf
float ReinhardTonemapping(float HDR, float k)
{
    return HDR / (HDR + k);
}

float ReinhardTonemappingPlot(void*, int index)
{
    return ReinhardTonemapping(index / (float)VALUES_COUNT * HDR_MAX, g_TonemapParameters.K);
}

float ReinhardSqrTonemappingPlot(void*, int index)
{
    float reinhard = ReinhardTonemapping(index / (float)VALUES_COUNT * HDR_MAX, g_TonemapParameters.K);
    return reinhard * reinhard;
}

// ACES Filmic
// See: https://www.slideshare.net/ozlael/hable-john-uncharted2-hdr-lighting/142
float ACESFilmicTonemapping(float x, float A, float B, float C, float D, float E, float F)
{
    return (((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - (E / F));
}

float ACESFilmicTonemappingPlot(void*, int index)
{
    float HDR = index / (float)VALUES_COUNT * HDR_MAX;
    return ACESFilmicTonemapping(HDR, g_TonemapParameters.A, g_TonemapParameters.B, g_TonemapParameters.C, g_TonemapParameters.D, g_TonemapParameters.E, g_TonemapParameters.F) /
        ACESFilmicTonemapping(g_TonemapParameters.LinearWhite, g_TonemapParameters.A, g_TonemapParameters.B, g_TonemapParameters.C, g_TonemapParameters.D, g_TonemapParameters.E, g_TonemapParameters.F);
}

void Sample4::OnGUI()
{
    static bool showDemoWindow = false;
    static bool showOptions = true;

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit", "Esc"))
            {
                PostQuitMessage(0);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow);
            ImGui::MenuItem("Tonemapping", nullptr, &showOptions);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Options") )
        {
            auto& app = Application::Get();

            bool vSync = app.IsVSync();
            if (ImGui::MenuItem("V-Sync", "V", &vSync))
            {
                app.SetVSync(vSync);
            }

            bool fullscreen = app.IsFullscreen();
            if (ImGui::MenuItem("Full screen", "Alt+Enter", &fullscreen) )
            {
                app.SetFullscreen(fullscreen);
            }

            ImGui::EndMenu();
        }

        char buffer[256];

        {
            // Output a slider to scale the resolution of the HDR render target.
            float renderScale = m_RenderScale;
            ImGui::PushItemWidth(300.0f);
            ImGui::SliderFloat("Resolution Scale", &renderScale, 0.1f, 2.0f);
            // Using Ctrl+Click on the slider, the user can set values outside of the 
            // specified range. Make sure to clamp to a sane range to avoid creating
            // giant render targets.
            renderScale = clamp(renderScale, 0.0f, 2.0f);

            // Output current resolution of render target.
            auto size = m_HDRRenderTarget.GetSize();
            ImGui::SameLine();
            sprintf_s(buffer, _countof(buffer), "(%ux%u)", size.x, size.y);
            ImGui::Text(buffer);

            // Resize HDR render target if the scale changed.
            if (renderScale != m_RenderScale)
            {
                m_RenderScale = renderScale;
                RescaleHDRRenderTarget(m_RenderScale);
            }
        }
        
        {
            const char* renderModeNames[] = { "Forward", "Deferred" };
            int currentMode = static_cast<int>(m_RenderingMode);

            if (ImGui::Combo("Rendering Mode", &currentMode, renderModeNames, 2))
            {
                m_RenderingMode = static_cast<RenderingMode>(currentMode);
            }

            ImGui::SameLine();
            ShowHelpMarker("Forward: Traditional forward rendering.\nDeferred: G-Buffer based deferred rendering.");
        }

        {
            sprintf_s(buffer, _countof(buffer), "FPS: %.2f (%.2f ms)  ", g_FPS, 1.0 / g_FPS * 1000.0);
            auto fpsTextSize = ImGui::CalcTextSize(buffer);
            ImGui::SameLine(ImGui::GetWindowWidth() - fpsTextSize.x);
            ImGui::Text(buffer);
        }

        ImGui::EndMainMenuBar();
    }

    if (showDemoWindow)
    {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    if (showOptions)
    {
        ImGui::Begin("Tonemapping", &showOptions);
        {
            ImGui::TextWrapped("Use the Exposure slider to adjust the overall exposure of the HDR scene.");
            ImGui::SliderFloat("Exposure", &g_TonemapParameters.Exposure, -10.0f, 10.0f);
            ImGui::SameLine(); ShowHelpMarker("Adjust the overall exposure of the HDR scene.");
            ImGui::SliderFloat("Gamma", &g_TonemapParameters.Gamma, 0.01f, 5.0f);
            ImGui::SameLine(); ShowHelpMarker("Adjust the Gamma of the output image.");

            const char* toneMappingMethods[] = {
                "Linear",
                "Reinhard",
                "Reinhard Squared",
                "ACES Filmic"
            };

            ImGui::Combo("Tonemapping Methods", (int*)(&g_TonemapParameters.TonemapMethod), toneMappingMethods, 4);

            switch (g_TonemapParameters.TonemapMethod)
            {
            case TonemapMethod::TM_Linear:
                ImGui::PlotLines("Linear Tonemapping", &LinearTonemappingPlot, nullptr, VALUES_COUNT, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 250));
                ImGui::SliderFloat("Max Brightness", &g_TonemapParameters.MaxLuminance, 1.0f, 10.0f);
                ImGui::SameLine(); ShowHelpMarker("Linearly scale the HDR image by the maximum brightness.");
                break;
            case TonemapMethod::TM_Reinhard:
                ImGui::PlotLines("Reinhard Tonemapping", &ReinhardTonemappingPlot, nullptr, VALUES_COUNT, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 250));
                ImGui::SliderFloat("Reinhard Constant", &g_TonemapParameters.K, 0.01f, 10.0f);
                ImGui::SameLine(); ShowHelpMarker("The Reinhard constant is used in the denominator.");
                break;
            case TonemapMethod::TM_ReinhardSq:
                ImGui::PlotLines("Reinhard Squared Tonemapping", &ReinhardSqrTonemappingPlot, nullptr, VALUES_COUNT, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 250));
                ImGui::SliderFloat("Reinhard Constant", &g_TonemapParameters.K, 0.01f, 10.0f);
                ImGui::SameLine(); ShowHelpMarker("The Reinhard constant is used in the denominator.");
                break;
            case TonemapMethod::TM_ACESFilmic:
                ImGui::PlotLines("ACES Filmic Tonemapping", &ACESFilmicTonemappingPlot, nullptr, VALUES_COUNT, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 250));
                ImGui::SliderFloat("Shoulder Strength", &g_TonemapParameters.A, 0.01f, 5.0f);
                ImGui::SliderFloat("Linear Strength", &g_TonemapParameters.B, 0.0f, 100.0f);
                ImGui::SliderFloat("Linear Angle", &g_TonemapParameters.C, 0.0f, 1.0f);
                ImGui::SliderFloat("Toe Strength", &g_TonemapParameters.D, 0.01f, 1.0f);
                ImGui::SliderFloat("Toe Numerator", &g_TonemapParameters.E, 0.0f, 10.0f);
                ImGui::SliderFloat("Toe Denominator", &g_TonemapParameters.F, 1.0f, 10.0f);
                ImGui::SliderFloat("Linear White", &g_TonemapParameters.LinearWhite, 1.0f, 120.0f);
                break;
            default:
                break;
            }
        }

        if (ImGui::Button("Reset to Defaults"))
        {
            TonemapMethod method = g_TonemapParameters.TonemapMethod;
            g_TonemapParameters = TonemapParameters();
            g_TonemapParameters.TonemapMethod = method;
        }

        ImGui::End();
    }
}

void XM_CALLCONV ComputeMatrices(FXMMATRIX model, CXMMATRIX view, CXMMATRIX viewProjection, Mat& mat)
{
    mat.ModelMatrix = model;
    mat.ModelViewMatrix = model * view;
    mat.InverseTransposeModelViewMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, mat.ModelViewMatrix));
    mat.ModelViewProjectionMatrix = model * viewProjection;
}

void Sample4::OnRender()
{
    Game::OnRender();

    auto& app = Application::Get();
    auto commandQueue = app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto commandList = commandQueue->GetCommandList();

    // 1. Clear the render targets.
    {
        FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };

        commandList->ClearTexture(m_HDRRenderTarget.GetTexture(AttachmentPoint::Color0), clearColor);
        commandList->ClearDepthStencilTexture(m_HDRRenderTarget.GetTexture(AttachmentPoint::DepthStencil), D3D12_CLEAR_FLAG_DEPTH);
    }

    commandList->SetRenderTarget(m_HDRRenderTarget);
    commandList->SetViewport(m_HDRRenderTarget.GetViewport());
    commandList->SetScissorRect(m_ScissorRect);

    // 2. Render the skybox.
    {
        // The view matrix should only consider the camera's rotation, but not the translation.
        auto viewMatrix = XMMatrixTranspose(XMMatrixRotationQuaternion(m_Camera.get_Rotation()));
        auto projMatrix = m_Camera.get_ProjectionMatrix();
        auto viewProjMatrix = viewMatrix * projMatrix;

        commandList->SetPipelineState(m_SkyboxPipelineState);
        commandList->SetGraphicsRootSignature(m_SkyboxSignature);

        commandList->SetGraphics32BitConstants(0, viewProjMatrix);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = m_GraceCathedralCubemap.GetD3D12ResourceDesc().Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = (UINT)-1; // Use all mips.

        // TODO: Need a better way to bind a cubemap.
        commandList->SetShaderResourceView(1, 0, m_GraceCathedralCubemap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, &srvDesc);

        m_SkyboxMesh->Draw(*commandList);
    }

	// 3. Render the scene (FBX model + debug shapes) in HDR to an off-screen render target, using either forward or deferred rendering.
    {
        XMMATRIX viewMatrix = m_Camera.get_ViewMatrix();
        XMMATRIX viewProjectionMatrix = viewMatrix * m_Camera.get_ProjectionMatrix();

        if (m_RenderingMode == RenderingMode::Forward)
        {
            RenderForward(commandList, viewMatrix, viewProjectionMatrix);
        }
        else // Deferred
        {
            RenderDeferred(commandList, viewMatrix, viewProjectionMatrix);
        }
    }

    // 4. FSQ Posteffect: perform HDR -> SDR tonemapping directly to the Window's render target.
    {
        commandList->SetRenderTarget(app.GetRenderTarget());
        commandList->SetViewport(app.GetRenderTarget().GetViewport());
        commandList->SetPipelineState(m_SDRPipelineState);
        commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->SetGraphicsRootSignature(m_SDRRootSignature);
        commandList->SetGraphics32BitConstants(0, g_TonemapParameters);
        commandList->SetShaderResourceView(1, 0, m_HDRRenderTarget.GetTexture(Color0), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        commandList->Draw(3);
    }

	// 5. Execute the command list.
    commandQueue->ExecuteCommandList(commandList);

    // 6. Render GUI.
    OnGUI();

    // 7. Present
    app.Present();
}

void Sample4::RenderForward(std::shared_ptr<CommandList> commandList, DirectX::CXMMATRIX viewMatrix, DirectX::CXMMATRIX viewProjectionMatrix)
{
    // 1. Draw FBX model with multiple mesh parts and materials.
    commandList->SetPipelineState(m_HDRPipelineState);
    commandList->SetGraphicsRootSignature(m_HDRRootSignature);

    // -- Upload lights properties to CB and SB --
    LightProperties lightProperties;
    lightProperties.NumPointLights = static_cast<uint32_t>(m_PointLights.size());
    lightProperties.NumSpotLights = static_cast<uint32_t>(m_SpotLights.size());

    commandList->SetGraphics32BitConstants(RootParameters::LightPropertiesCB, lightProperties);
    commandList->SetGraphicsDynamicStructuredBuffer(RootParameters::PointLights, m_PointLights);
    commandList->SetGraphicsDynamicStructuredBuffer(RootParameters::SpotLights, m_SpotLights);

	// RENDER FBX MODEL
    {
        float scale = 1 / 10.0f;

        XMMATRIX translationMatrix = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
        XMMATRIX rotationMatrix = XMMatrixIdentity();
	    XMMATRIX scaleMatrix = XMMatrixScaling(scale, scale, scale);
        XMMATRIX worldMatrix = scaleMatrix * rotationMatrix * translationMatrix;

        Mat matrices;
        ComputeMatrices(worldMatrix, viewMatrix, viewProjectionMatrix, matrices);
        // --
        commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MatricesCB, matrices);

        // Returns the frustum in view (camera) space
        // --
        // The projection matrix transforms from view space -> clip/projection space.
        // So when you create a frustum from the projection matrix, the resulting planes represent the 
        // view volume boundaries in the coordinate system where the camera is at the origin — which is view space.
        DirectX::BoundingFrustum cameraFrustum;
        DirectX::BoundingFrustum::CreateFromMatrix(cameraFrustum, m_Camera.get_ProjectionMatrix());

        // Transform the Frustum from View to World space
        XMMATRIX inverseView = XMMatrixInverse(nullptr, viewMatrix);
        cameraFrustum.Transform(cameraFrustum, inverseView);

        Texture* currentTexture = nullptr;
        Material* currentMaterial = nullptr;
        for (auto& part : m_LoadedMeshParts)
        {
            // Transform the mesh part's bounding box to World space
            DirectX::BoundingBox worldBounds;
            part.boundingBox.Transform(worldBounds, worldMatrix);

            // [OPT_1] Frustum culling in World space - cameraFrustum vs AABB of mesh part
            if (!worldBounds.Intersects(cameraFrustum))
                continue;

            if (&part.material != currentMaterial)
            {
                commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MaterialCB, part.material);
                currentMaterial = &part.material;
            }

            // [OPT_2] Only change texture if different
            if (&part.diffuseTexture != currentTexture)
            {
                commandList->SetShaderResourceView(RootParameters::Textures, 0, part.diffuseTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                currentTexture = &part.diffuseTexture;
            }

            part.mesh->Draw(*commandList);
        }
    }

#if 0
    // 2. Draw shapes to visualize the position of the POINT lights in the scene.

    Material lightMaterial;
    // No specular
    lightMaterial.Specular = { 0, 0, 0, 1 };
    for (const auto& l : m_PointLights)
    {
        lightMaterial.Emissive = l.Color;
        XMVECTOR lightPos = XMLoadFloat4(&l.PositionWS);
        worldMatrix = XMMatrixTranslationFromVector(lightPos);
        ComputeMatrices(worldMatrix, viewMatrix, viewProjectionMatrix, matrices);

        commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MatricesCB, matrices);
        commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MaterialCB, lightMaterial);

        m_SphereMesh->Draw(*commandList);
    }

    // 3. Draw cones to visualize the position of the SPOT lights in the scene.

    for (const auto& l : m_SpotLights)
    {
        lightMaterial.Emissive = l.Color;
        XMVECTOR lightPos = XMLoadFloat4(&l.PositionWS);
        XMVECTOR lightDir = XMLoadFloat4(&l.DirectionWS);
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);

        // Rotate the cone so it is facing the Z axis.
        rotationMatrix = XMMatrixRotationX(XMConvertToRadians(-90.0f));
        worldMatrix = rotationMatrix * LookAtMatrix(lightPos, lightDir, up);

        ComputeMatrices(worldMatrix, viewMatrix, viewProjectionMatrix, matrices);

        commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MatricesCB, matrices);
        commandList->SetGraphicsDynamicConstantBuffer(RootParameters::MaterialCB, lightMaterial);

        m_ConeMesh->Draw(*commandList);
    }
#endif
}

void Sample4::RenderDeferred(std::shared_ptr<CommandList> commandList, DirectX::CXMMATRIX viewMatrix, DirectX::CXMMATRIX viewProjectionMatrix)
{
    // =======================================================================================================================
	//                  TODO - render skybox directly to Deferred Lighting RT before this pass
    //                         We don't actually need a separate HDR RT, we can directly render into Deferred Lighting RT
    // =======================================================================================================================


    // === PASS 1: G-Buffer Generation ===

    commandList->SetRenderTarget(m_GBufferRT);
    commandList->SetViewport(m_GBufferRT.GetViewport());

    // Clear G-Buffer
    FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    commandList->ClearTexture(m_GBufferRT.GetTexture(AttachmentPoint::Color0), clearColor);
    commandList->ClearTexture(m_GBufferRT.GetTexture(AttachmentPoint::Color1), clearColor);
    commandList->ClearTexture(m_GBufferRT.GetTexture(AttachmentPoint::Color2), clearColor);
    commandList->ClearDepthStencilTexture(m_GBufferRT.GetTexture(AttachmentPoint::DepthStencil), D3D12_CLEAR_FLAG_DEPTH);

    // Set G-Buffer PSO
    commandList->SetPipelineState(m_GBufferPSO);
    commandList->SetGraphicsRootSignature(m_GBufferRootSignature);

    // Render meshes into G-Buffer (same loop as forward, but no lights needed)
    // RENDER FBX MODEL
    // -- TODO -- DREMOVE DUPLICATION - NEED TO USE UNIFIED RootParameters to share code
    {
        float scale = 1 / 10.0f;

        XMMATRIX translationMatrix = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
        XMMATRIX rotationMatrix = XMMatrixIdentity();
        XMMATRIX scaleMatrix = XMMatrixScaling(scale, scale, scale);
        XMMATRIX worldMatrix = scaleMatrix * rotationMatrix * translationMatrix;

        Mat matrices;
        ComputeMatrices(worldMatrix, viewMatrix, viewProjectionMatrix, matrices);
        // --
        commandList->SetGraphicsDynamicConstantBuffer(GbufferRootParams::MatricesCB_GBuffer, matrices);

        // Returns the frustum in view (camera) space
        // --
        // The projection matrix transforms from view space -> clip/projection space.
        // So when you create a frustum from the projection matrix, the resulting planes represent the 
        // view volume boundaries in the coordinate system where the camera is at the origin — which is view space.
        DirectX::BoundingFrustum cameraFrustum;
        DirectX::BoundingFrustum::CreateFromMatrix(cameraFrustum, m_Camera.get_ProjectionMatrix());

        // Transform the Frustum from View to World space
        XMMATRIX inverseView = XMMatrixInverse(nullptr, viewMatrix);
        cameraFrustum.Transform(cameraFrustum, inverseView);

        Texture* currentTexture = nullptr;
        Material* currentMaterial = nullptr;
        for (auto& part : m_LoadedMeshParts)
        {
            // Transform the mesh part's bounding box to World space
            DirectX::BoundingBox worldBounds;
            part.boundingBox.Transform(worldBounds, worldMatrix);

            // [OPT_1] Frustum culling in World space - cameraFrustum vs AABB of mesh part
            if (!worldBounds.Intersects(cameraFrustum))
                continue;

            if (&part.material != currentMaterial)
            {
                commandList->SetGraphicsDynamicConstantBuffer(GbufferRootParams::MaterialCB_GBuffer, part.material);
                currentMaterial = &part.material;
            }

            // [OPT_2] Only change texture if different
            if (&part.diffuseTexture != currentTexture)
            {
                commandList->SetShaderResourceView(GbufferRootParams::Textures_GBuffer, 0, part.diffuseTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                currentTexture = &part.diffuseTexture;
            }

            part.mesh->Draw(*commandList);
        }
    }

    // === PASS 2: Deferred Lighting ===

    // Set deferred lighting output as render target
    commandList->SetRenderTarget(m_DeferredLightingRT);
    commandList->SetPipelineState(m_DeferredLightingPSO);
    commandList->SetGraphicsRootSignature(m_DeferredLightingRootSignature);

    // Bind lights
    LightProperties lightProperties;
    lightProperties.NumPointLights = static_cast<uint32_t>(m_PointLights.size());
    lightProperties.NumSpotLights = static_cast<uint32_t>(m_SpotLights.size());

    commandList->SetGraphics32BitConstants(DeferredRootParams::LightPropertiesCB_Deferred, lightProperties);
    commandList->SetGraphicsDynamicStructuredBuffer(DeferredRootParams::PointLights_Deferred, m_PointLights);
    commandList->SetGraphicsDynamicStructuredBuffer(DeferredRootParams::SpotLights_Deferred, m_SpotLights);

    // Bind G-Buffer textures
    commandList->SetShaderResourceView(DeferredRootParams::Textures_Deferred, 0, m_GBufferRT.GetTexture(AttachmentPoint::Color0), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->SetShaderResourceView(DeferredRootParams::Textures_Deferred, 1, m_GBufferRT.GetTexture(AttachmentPoint::Color1), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->SetShaderResourceView(DeferredRootParams::Textures_Deferred, 2, m_GBufferRT.GetTexture(AttachmentPoint::Color2), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Draw full-screen triangle
    commandList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->Draw(3); // Full-screen triangle

    // === PASS 3: COPY (deferred lighting result to HDR rt for tone mapping ===
    commandList->CopyResource(m_HDRRenderTarget.GetTexture(AttachmentPoint::Color0), m_DeferredLightingRT.GetTexture(AttachmentPoint::Color0));
}

static bool g_AllowFullscreenToggle = true;

void Sample4::OnKeyPressed(KeyEventArgs& e)
{
    Game::OnKeyPressed(e);

    Application* pApp = &Application::Get();

    if (pApp && !ImGui::GetIO().WantCaptureKeyboard)
    {
        switch (e.Key)
        {
        case KeyCode::Escape:
            PostQuitMessage(0);
            break;
        case KeyCode::Enter:
            if (e.Alt)
            {
        case KeyCode::F11:
            if (g_AllowFullscreenToggle)
            {
                pApp->ToggleFullscreen();
                g_AllowFullscreenToggle = false;
            }
            break;
            }
        case KeyCode::V:
            pApp->ToggleVSync();
            break;
        case KeyCode::R:
            // Reset camera transform
            m_Camera.set_Translation(m_pAlignedCameraData->m_InitialCamPos);
            m_Camera.set_Rotation(m_pAlignedCameraData->m_InitialCamRot);
            m_Camera.set_FoV(m_pAlignedCameraData->m_InitialFov);
            m_Pitch = 0.0f;
            m_Yaw = 0.0f;
            break;
        case KeyCode::Up:
        case KeyCode::W:
            m_Forward = 1.0f;
            break;
        case KeyCode::Left:
        case KeyCode::A:
            m_Left = 1.0f;
            break;
        case KeyCode::Down:
        case KeyCode::S:
            m_Backward = 1.0f;
            break;
        case KeyCode::Right:
        case KeyCode::D:
            m_Right = 1.0f;
            break;
        case KeyCode::Q:
            m_Down = 1.0f;
            break;
        case KeyCode::E:
            m_Up = 1.0f;
            break;
        case KeyCode::Space:
            m_AnimateLights = !m_AnimateLights;
            break;
        case KeyCode::ShiftKey:
            m_Shift = true;
            break;
        }
    }
}

void Sample4::OnKeyReleased(KeyEventArgs& e)
{
    Game::OnKeyReleased(e);

    if (!ImGui::GetIO().WantCaptureKeyboard)
    {
        switch (e.Key)
        {
        case KeyCode::Enter:
            if (e.Alt)
            {
        case KeyCode::F11:
            g_AllowFullscreenToggle = true;
            }
            break;
        case KeyCode::Up:
        case KeyCode::W:
            m_Forward = 0.0f;
            break;
        case KeyCode::Left:
        case KeyCode::A:
            m_Left = 0.0f;
            break;
        case KeyCode::Down:
        case KeyCode::S:
            m_Backward = 0.0f;
            break;
        case KeyCode::Right:
        case KeyCode::D:
            m_Right = 0.0f;
            break;
        case KeyCode::Q:
            m_Down = 0.0f;
            break;
        case KeyCode::E:
            m_Up = 0.0f;
            break;
        case KeyCode::ShiftKey:
            m_Shift = false;
            break;
        }
    }
}

void Sample4::OnMouseMoved(MouseMotionEventArgs& e)
{
    Game::OnMouseMoved(e);

    const float mouseSpeed = 0.1f;
    if (!ImGui::GetIO().WantCaptureMouse)
    {
        if (e.LeftButton)
        {
            m_Pitch += e.RelY * mouseSpeed;

            m_Pitch = clamp(m_Pitch, -90.0f, 90.0f);

            m_Yaw += e.RelX * mouseSpeed;
        }
    }
}


void Sample4::OnMouseWheel(MouseWheelEventArgs& e)
{
    Game::OnMouseWheel(e);

    if (!ImGui::GetIO().WantCaptureMouse)
    {
        auto fov = m_Camera.get_FoV();

        fov -= e.WheelDelta;
        fov = clamp(fov, 12.0f, 90.0f);

        m_Camera.set_FoV(fov);

        char buffer[256];
        sprintf_s(buffer, "FoV: %f\n", fov);
        OutputDebugStringA(buffer);
    }
}
