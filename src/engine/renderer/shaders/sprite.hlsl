// Default 2D shader: textured, tinted quads in pixel coordinates.

cbuffer Frame : register(b0)
{
    float4x4 projection;
};

Texture2D sprite_texture : register(t0);
SamplerState sprite_sampler : register(s0);

struct VertexIn
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VertexOut vs_main(VertexIn input)
{
    VertexOut output;
    output.position = mul(projection, float4(input.position, 0.0, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 ps_main(VertexOut input) : SV_TARGET
{
    return sprite_texture.Sample(sprite_sampler, input.uv) * input.color;
}
