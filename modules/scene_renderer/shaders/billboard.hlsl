struct Vertex { float4 position : POSITION; float2 uv : TEXCOORD0; };
struct Pixel { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Texture2D marker : register(t0);
SamplerState markerSampler : register(s0);
Pixel vs_main(Vertex v) { Pixel p; p.position = v.position; p.uv = v.uv; return p; }
float4 ps_main(Pixel p) : SV_TARGET
{
    float4 color = marker.Sample(markerSampler, p.uv);
    clip(color.a - 0.01);
    float3 display = lerp(color.rgb * 12.92, 1.055 * pow(color.rgb, 1.0 / 2.4) - 0.055, step(0.0031308, color.rgb));
    return float4(display, 1);
}
