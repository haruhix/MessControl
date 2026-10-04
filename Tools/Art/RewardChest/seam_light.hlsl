float stripe = exp(-pow((UV.y-0.5)*7.0,2.0));
float pulse = 0.86+0.14*sin(Time*1.6);
return float3(10.0,5.8,0.55)*stripe*pulse;
