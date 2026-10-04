float2 p = float2((UV.x-0.5)*14.0,(1.0-UV.y)*22.0+44.0);
float head = 1.0-smoothstep(4.5,5.3,length(p-float2(0.0,59.0)));
float stemWidth = lerp(3.4,1.7,saturate((p.y-45.5)/10.0));
float stem = (1.0-smoothstep(stemWidth,stemWidth+0.6,abs(p.x)))
              *smoothstep(45.0,46.0,p.y)*(1.0-smoothstep(54.0,55.5,p.y));
return float3(9.0,5.5,0.4)*max(head,stem)*(0.87+0.13*sin(Time*1.6));
