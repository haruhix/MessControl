float3 facing = normalize(Camera-Center);
float3 worldUp = abs(facing.z)>0.98 ? float3(0,1,0) : float3(0,0,1);
float3 right = normalize(cross(worldUp,facing));
float3 up = cross(facing,right);
float angle = Time*Speed*0.052+0.06*sin(Time*Speed*0.37);
float2 p = (UV-0.5)*440.0;
p = float2(p.x*cos(angle)-p.y*sin(angle),p.x*sin(angle)+p.y*cos(angle));
return Center+(right*p.x+up*p.y-facing*83.0)*Scale-World;
