cbuffer SceneParameters : register(b0)
{
    float4x4 model;
    float4x4 viewProjection;
    float4x4 normalTransform;
    float4 baseColor;
    float4 lightDirection;
    float4 ambient;
    float4 directional;
};
struct Vertex { float3 position : POSITION; float3 normal : NORMAL; };
struct Pixel { float4 position : SV_POSITION; float3 normal : NORMAL; };
Pixel vs_main(Vertex v)
{
    Pixel p;
    p.position = mul(viewProjection, mul(model, float4(v.position, 1)));
    p.normal = mul(normalTransform, float4(v.normal, 0)).xyz;
    return p;
}
float4 ps_main(Pixel p) : SV_TARGET
{
    float3 linearColor = saturate(baseColor.rgb * (ambient.rgb + directional.rgb * max(dot(normalize(p.normal), lightDirection.xyz), 0)));
    float3 display = lerp(linearColor * 12.92, 1.055 * pow(linearColor, 1.0 / 2.4) - 0.055, step(0.0031308, linearColor));
    return float4(display, 1);
}
