// Pre-skinned centimeters keep the fracture pattern attached to the rig.
struct ToothFracture {
    float3 hash3(float3 p) {
        p = frac(p * float3(.1031,.1030,.0973));
        p += dot(p,p.yxz + 33.33);
        return frac((p.xxy + p.yxx) * p.zyx);
    }
    float hash(float3 p) { return hash3(p).x; }
    float noise(float3 p) {
        float3 i = floor(p), f = frac(p);
        f = f*f*(3-2*f);
        float a = lerp(hash(i),hash(i+float3(1,0,0)),f.x);
        float b = lerp(hash(i+float3(0,1,0)),hash(i+float3(1,1,0)),f.x);
        float c = lerp(hash(i+float3(0,0,1)),hash(i+float3(1,0,1)),f.x);
        float d = lerp(hash(i+float3(0,1,1)),hash(i+float3(1,1,1)),f.x);
        return lerp(lerp(a,b,f.y),lerp(c,d,f.y),f.z);
    }
    float2 cellEdge(float3 p) {
        float3 cell = floor(p), local = frac(p);
        float first = 1e6, second = 1e6, seed = 0;
        [unroll] for (int z=-1; z<=1; ++z) {
            [unroll] for (int y=-1; y<=1; ++y) {
                [unroll] for (int x=-1; x<=1; ++x) {
                    float3 offset = float3(x,y,z);
                    float3 random = hash3(cell+offset);
                    float3 r = offset + .18 + random*.64 - local;
                    float distance = dot(r,r);
                    if (distance<first) {
                        second = first; first = distance; seed = random.x;
                    } else { second = min(second,distance); }
                }
            }
        }
        return float2((sqrt(second)-sqrt(first))*.5,seed);
    }
    float segment(float2 p,float2 a,float2 b) {
        float2 ab = b-a;
        float t = saturate(dot(p-a,ab)/max(dot(ab,ab),.001));
        return length(p-a-ab*t);
    }
    float protect(float3 p,float3 center,float3 radius) {
        return 1-smoothstep(.85,1.12,length((p-center)/radius));
    }
};
ToothFracture F;
float damage = saturate(Damage);
[branch] if (damage<=.0001) return float4(0,0,0,0);
float3 drift = float3(F.noise(P*.10),F.noise(P*.10+17.1),F.noise(P*.10+31.7))-.5;
float3 splinters = float3(F.noise(P*.55),F.noise(P*.55+12.7),F.noise(P*.55+24.3))-.5;
float size = max(CellSize,8);
float2 cells = F.cellEdge((P+drift*4.8+splinters*.9)/size+float3(5.13,2.71,9.23));
float distance = max(0,cells.x*size + (F.noise(P*.95)-.5)*.12);
float fineWidth = lerp(.035,.13,smoothstep(.03,.7,damage))*max(HairlineWidth,.2);
float antialias = max(fwidth(distance)*.65,.015);
float reveal = smoothstep(.035+cells.y*.32,.23+cells.y*.32,damage);
float fine = (1-smoothstep(fineWidth-antialias,fineWidth+antialias,distance))*reveal;

// Front faults are restricted to the front half of the tooth. Projecting
// these X/Z paths through the whole mesh produced mirrored bands on the back.
float2 p = P.xz + drift.xz*.9 + splinters.xz*.5;
float major = F.segment(p,float2(0,116),float2(-.3,104));
major = min(major,F.segment(p,float2(-.3,104),float2(1.8,99)));
major = min(major,F.segment(p,float2(1.8,99),float2(-.4,94)));
major = min(major,F.segment(p,float2(-.4,94),float2(-2.3,88)));
major = min(major,F.segment(p,float2(-2.3,88),float2(1.2,82)));
major = min(major,F.segment(p,float2(1.2,82),float2(.2,76)));
major = min(major,F.segment(p,float2(-31,116),float2(-28,106)));
major = min(major,F.segment(p,float2(-28,106),float2(-34,102)));
major = min(major,F.segment(p,float2(-34,102),float2(-43,100)));
major = min(major,F.segment(p,float2(41,57),float2(34,54)));
major = min(major,F.segment(p,float2(34,54),float2(31,48)));
major = min(major,F.segment(p,float2(31,48),float2(24,47)));
major = min(major,F.segment(p,float2(37,19),float2(29,18)));
major = min(major,F.segment(p,float2(29,18),float2(26,13)));
major = min(major,F.segment(p,float2(26,13),float2(18,10)));
major = min(major,F.segment(p,float2(-37,17),float2(-30,15)));
major = min(major,F.segment(p,float2(-30,15),float2(-24,18)));
major = min(major,F.segment(p,float2(-24,18),float2(-19,14)));
float growth = smoothstep(.24,.88,damage);
float taper = P.z>72 ? lerp(.16,1.0,smoothstep(77,108,P.z)) : .65;
float width = max(MajorWidth,.1)*growth*taper*lerp(.75,1.25,F.noise(P*.7));
float aa = max(fwidth(major)*.55,.025);
float deep = (1-smoothstep(width-aa,width+aa,major))*growth;
float rim = smoothstep(width-aa*.3,width+aa*.6,major)
    * (1-smoothstep(width+.18,width+.55,major))*growth;
float front = smoothstep(-2,12,P.y);
deep *= front;
rim *= front;

// The back has two separate, open faults with tapered tips. They neither
// span the body nor trace closed outlines around the existing sculpt.
float backA = F.segment(p,float2(16,108),float2(12.5,103));
backA = min(backA,F.segment(p,float2(12.5,103),float2(15,99)));
backA = min(backA,F.segment(p,float2(15,99),float2(11,94)));
backA = min(backA,F.segment(p,float2(11,94),float2(12,91)));
float taperA = smoothstep(91,97,P.z)*(1-smoothstep(103,109,P.z));
float backB = F.segment(p,float2(-27,66),float2(-23,62));
backB = min(backB,F.segment(p,float2(-23,62),float2(-24.5,57)));
backB = min(backB,F.segment(p,float2(-24.5,57),float2(-19,54)));
backB = min(backB,F.segment(p,float2(-19,54),float2(-17,49)));
float taperB = smoothstep(49,55,P.z)*(1-smoothstep(61,67,P.z));
float backWidth = max(MajorWidth,.1)*.43*growth*lerp(.65,1.15,F.noise(P*.8));
float aaA = max(fwidth(backA)*.55,.025);
float aaB = max(fwidth(backB)*.55,.025);
float cutA = (1-smoothstep(backWidth*taperA-aaA,backWidth*taperA+aaA,backA))*taperA;
float cutB = (1-smoothstep(backWidth*taperB-aaB,backWidth*taperB+aaB,backB))*taperB;
float edgeA = smoothstep(backWidth*taperA,backWidth*taperA+aaA,backA)
    * (1-smoothstep(backWidth*taperA+.10,backWidth*taperA+.30,backA))*taperA;
float edgeB = smoothstep(backWidth*taperB,backWidth*taperB+aaB,backB)
    * (1-smoothstep(backWidth*taperB+.10,backWidth*taperB+.30,backB))*taperB;
float back = (1-smoothstep(-18,-4,P.y))*growth;
deep = max(deep,max(cutA,cutB)*back*.72);
rim = max(rim,max(edgeA,edgeB)*back*.6);
float enamel = smoothstep(.20,.48,min(Enamel.r,min(Enamel.g,Enamel.b)));
float face = max(
    F.protect(P,float3(17.67,20.21,80.20),float3(17,34,20)),
    F.protect(P,float3(-17.67,20.21,80.20),float3(17,34,20)));
face = max(face,F.protect(P,float3(0,27,55),float3(18,17,11)));
float surface = enamel*(1-face)*smoothstep(0,.035,damage);
fine *= surface;
deep *= surface;
rim *= surface;
// Z is a recessed height in cm; W is the intact enamel bevel.
return float4(fine,deep,-fine*.025-deep*.70+rim*.055,rim);
