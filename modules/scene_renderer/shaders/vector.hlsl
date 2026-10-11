struct Input { float4 position : POSITION; float3 color : NORMAL; };
struct Output { float4 position : SV_POSITION; float3 color : COLOR0; };
Output vs_main(Input input) { Output o; o.position=input.position; o.color=input.color; return o; }
float4 ps_main(Output input) : SV_TARGET { return float4(input.color, 1); }
