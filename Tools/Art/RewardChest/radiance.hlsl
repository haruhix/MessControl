float2 p = (UV - 0.5) * 2.0;
float r = length(p);
float angle = atan2(p.y, p.x);
float rays = 0.0;
// Two independently drifting groups of soft shafts, with finer bright centers.
for (int layer = 0; layer < 2; ++layer)
{
    float count = layer == 0 ? 29.0 : 41.0;
    float a = (angle + Time * (layer == 0 ? 0.052 : -0.027)
               + 0.13 * sin(Time * 0.37)) / 6.2831853;
    float cell = floor(frac(a) * count);
    float d = frac(a * count) - 0.5;
    float h = frac(sin(cell * 127.1 + layer * 311.7) * 43758.5453);
    float extent = 0.50 + h * 0.49;
    float width = 0.065 + 0.12 * h;
    float shaft = exp(-d*d/(width*width));
    float spine = exp(-d*d/(0.018*0.018));
    float lengthFade = pow(saturate(1.0-r/extent), 1.35);
    float flow = 0.79 + 0.21*sin(r*16.0-Time*1.8+cell*2.1);
    rays += (shaft * 0.95 + spine * 0.16) * lengthFade * flow
             * smoothstep(0.04, 0.16, r);
}
float corona = exp(-r*r*12.0) * 0.90 + exp(-r*r*3.8) * 0.11;
float pulse = 0.90 + 0.10*sin(Time*1.6);
float edge = 1.0-smoothstep(0.84,1.0,r);
float3 gold = lerp(float3(4.5,1.7,0.10), float3(8.0,4.8,0.55), exp(-r*r*9.0));
return gold * (rays + corona) * pulse * edge;
