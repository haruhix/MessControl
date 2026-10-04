float id = floor(UV.x);
float h = frac(sin(id*127.1+17.7)*43758.5453);
float h2 = frac(sin(id*311.7+41.3)*23421.631);
float life = frac(Time*(0.08+h*0.055)+h2);
float2 p = abs((float2((frac(UV.x)-0.125)/0.75,UV.y)-0.5)*2.0);
float disc = exp(-dot(p,p)*5.8);
float star = pow(saturate(1.0-sqrt(p.x)-sqrt(p.y)),0.7)
           + exp(-dot(p,p)*14.0)*0.3;
float shape = h2>0.78 ? star : disc;
float fade = smoothstep(0.0,0.12,life)*(1.0-smoothstep(0.75,1.0,life));
float twinkle = pow(0.5+0.5*sin(Time*(2.2+h*2.5)+id*1.7),2.0);
return float3(18.0,9.3,0.9)*shape*fade*(0.38+0.62*twinkle);
