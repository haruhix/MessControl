float3 facing = normalize(Camera-Center);
float3 worldUp = abs(facing.z)>0.98 ? float3(0,1,0) : float3(0,0,1);
float3 right = normalize(cross(worldUp, facing));
float3 up = cross(facing, right);
// The symmetrical source quad also measures inherited scale, including placed chests.
float inheritedScale = length(World-Center) / 311.126984;
float2 p = (UV-0.5)*440.0;
float3 target = Center + (right*p.x+up*p.y-facing*83.0)*inheritedScale;
return target-World;
