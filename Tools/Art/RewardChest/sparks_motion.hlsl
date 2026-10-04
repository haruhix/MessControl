float id = floor(UV.x);
float h = frac(sin(id*127.1+17.7)*43758.5453);
float h2 = frac(sin(id*311.7+41.3)*23421.631);
float life = frac(Time*(0.08+h*0.055)+h2);
float2 p = float2((h*2.0-1.0)*177.0 + sin(Time*0.6+id*2.3)*11.0,
                  -46.0+life*207.0);
float size = h2>0.78 ? 5.2 : 1.4+h2*1.6;
p += (float2((frac(UV.x)-0.125)/0.75,UV.y)-0.5)*size*2.0;
float3 facing = normalize(Camera-Center);
float3 worldUp = abs(facing.z)>0.98 ? float3(0,1,0) : float3(0,0,1);
float3 right = normalize(cross(worldUp,facing));
float3 up = cross(facing,right);
float inheritedScale = length(World-Center)/311.126984;
return Center+(right*p.x+up*p.y+facing*60.0)*inheritedScale-World;
