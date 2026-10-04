float3 facing = Camera-Center;
facing.z = 0;
float3 right = normalize(cross(float3(0,0,1),normalize(facing)));
float2 p = float2((UV.x-0.5)*210.0,(0.5-UV.y)*410.0);
return Center+(right*p.x+float3(0,0,p.y))*Scale-World;
