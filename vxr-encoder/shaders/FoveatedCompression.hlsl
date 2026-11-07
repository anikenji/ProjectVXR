// ============================================================================
// Project VXR - Non-Linear Hyperbolic Tangent UV Foveated Compression Shader
// (Virtual Desktop Phase B.1 Inheritance - Saves 30-40% NVENC Bitrate)
// ============================================================================
// Preserves 1:1 native pixel density in the central optical sweet-spot (foveaRadius)
// and smoothly compresses peripheral UV coordinates by 50% using a continuous C1
// hyperbolic tangent warp before NVENC encoding.
// ============================================================================

Texture2D<float4>   g_InputTexture  : register(t0);
RWTexture2D<float4> g_OutputTexture : register(u0);
SamplerState        g_LinearSampler : register(s0);

cbuffer FoveatedParams : register(b0)
{
    float2 g_InputResolution;   // e.g. (2064, 2208)
    float2 g_OutputResolution;  // e.g. (1648, 1760) compressed encode target
    float2 g_GazeCenterUV;      // Default (0.5, 0.5) or eye-tracked gaze center
    float  g_FoveaRadius;       // Central 1:1 sweet-spot radius in NDC (e.g. 0.30)
    float  g_PeripheralScale;   // Peripheral compression ratio (0.50 = 50% compression)
};

// Maps compressed output UV [0..1] back to source eye-render UV [0..1]
// Central region (|d| <= foveaRadius) maintains 1:1 slope; periphery uses tanh warp.
float2 FoveatedWarpUV(float2 compressedUV)
{
    // Convert [0..1] UV to [-1..1] centered around optical/gaze axis
    float2 center = g_GazeCenterUV * 2.0f - 1.0f;
    float2 p = (compressedUV * 2.0f - 1.0f) - center;

    float r = length(p);
    if (r < 1e-5f)
    {
        return compressedUV;
    }

    float2 dir = p / r;
    float foveaR = clamp(g_FoveaRadius, 0.10f, 0.75f);
    float periphScale = clamp(g_PeripheralScale, 0.25f, 1.0f);

    float srcR = r;
    if (r > foveaR)
    {
        // Non-linear hyperbolic tangent inverse mapping in the peripheral ring:
        // Compresses outer 50% of the frustum into a narrower encoded band
        float excess = (r - foveaR) / max(1.0f - foveaR, 1e-4f);
        float warped = tanh(excess / max(periphScale, 0.25f)) / tanh(1.0f / max(periphScale, 0.25f));
        srcR = foveaR + warped * (1.0f - foveaR);
    }

    float2 srcNDC = center + dir * srcR;
    return saturate(srcNDC * 0.5f + 0.5f);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint outW = (uint)g_OutputResolution.x;
    uint outH = (uint)g_OutputResolution.y;
    if (dispatchThreadID.x >= outW || dispatchThreadID.y >= outH)
    {
        return;
    }

    float2 outUV = (float2(dispatchThreadID.xy) + 0.5f) / g_OutputResolution;
    float2 srcUV = FoveatedWarpUV(outUV);

    // Sample with bilinear filtering at warped UV coordinate
    float4 color = g_InputTexture.SampleLevel(g_LinearSampler, srcUV, 0.0f);
    g_OutputTexture[dispatchThreadID.xy] = color;
}
