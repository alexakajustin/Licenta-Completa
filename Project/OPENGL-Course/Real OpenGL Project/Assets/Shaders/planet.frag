#version 410

in vec3 WorldPos;
in vec2 TexCoord;
in vec3 Normal;
in vec3 LocalPos;

out vec4 color;

uniform vec3 eyePosition;
uniform float time;

// Biome levels
uniform float seaLevel = 0.45;
uniform float sandLevel = 0.48;
uniform float grassLevel = 0.6;
uniform float rockLevel = 0.8;
uniform float snowLevel = 0.9;

// Noise settings
uniform float noiseScale = 1.0;
uniform int octaves = 6;
uniform float persistence = 0.5;
uniform float lacunarity = 2.0;
uniform int seed = 0;

// Colors
const vec3 deepOcean = vec3(0.0, 0.1, 0.3);
const vec3 shallowOcean = vec3(0.0, 0.4, 0.6);
const vec3 sand = vec3(0.8, 0.7, 0.5);
const vec3 grass = vec3(0.2, 0.5, 0.1);
const vec3 forest = vec3(0.1, 0.3, 0.05);
const vec3 rock = vec3(0.4, 0.4, 0.4);
const vec3 snow = vec3(0.95, 0.95, 1.0);

// Simplex 3D Noise implementation
vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x*34.0)+1.0)*x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise(vec3 v)
{
  const vec2  C = vec2(1.0/6.0, 1.0/3.0) ;
  const vec4  D = vec4(0.0, 0.5, 1.0, 2.0);
  vec3 i  = floor(v + dot(v, C.yyy) );
  vec3 x0 =   v - i + dot(i, C.xxx) ;
  vec3 g = step(x0.yzx, x0.xyz);
  vec3 l = 1.0 - g;
  vec3 i1 = min( g.xyz, l.zxy );
  vec3 i2 = max( g.xyz, l.zxy );
  vec3 x1 = x0 - i1 + C.xxx;
  vec3 x2 = x0 - i2 + C.yyy;
  vec3 x3 = x0 - D.yyy;
  i = mod289(i);
  vec4 p = permute( permute( permute(
             i.z + vec4(0.0, i1.z, i2.z, 1.0 ))
           + i.y + vec4(0.0, i1.y, i2.y, 1.0 ))
           + i.x + vec4(0.0, i1.x, i2.x, 1.0 ));
  float n_ = 0.142857142857;
  vec3  ns = n_ * D.wyz - D.xzx;
  vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
  vec4 x_ = floor(j * ns.z);
  vec4 y_ = floor(j - 7.0 * x_ );
  vec4 x = x_ *ns.x + ns.yyyy;
  vec4 y = y_ *ns.x + ns.yyyy;
  vec4 h = 1.0 - abs(x) - abs(y);
  vec4 b0 = vec4( x.xy, y.xy );
  vec4 b1 = vec4( x.zw, y.zw );
  vec4 s0 = floor(b0)*2.0 + 1.0;
  vec4 s1 = floor(b1)*2.0 + 1.0;
  vec4 sh = -step(h, vec4(0.0));
  vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy ;
  vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww ;
  vec3 p0 = vec3(a0.xy,h.x);
  vec3 p1 = vec3(a0.zw,h.y);
  vec3 p2 = vec3(a1.xy,h.z);
  vec3 p3 = vec3(a1.zw,h.w);
  vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2, p2), dot(p3,p3)));
  p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;
  vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
  m = m * m;
  return 42.0 * dot( m*m, vec4( dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3) ) );
}

float fBm(vec3 p)
{
    float val = 0.0;
    float amp = 0.5;
    float freq = noiseScale;
    vec3 offset = vec3(float(seed) * 0.123, float(seed) * 0.456, float(seed) * 0.789);
    for(int i = 0; i < octaves; i++) {
        val += amp * snoise((p + offset) * freq);
        amp *= persistence;
        freq *= lacunarity;
    }
    return val;
}

void main()
{
    vec3 n = normalize(Normal);
    vec3 lPos = normalize(LocalPos);
    
    // Generate height from noise
    float h = fBm(lPos) * 0.5 + 0.5;
    
    // Biome coloring
    vec3 finalColor;
    if (h < seaLevel) {
        finalColor = mix(deepOcean, shallowOcean, h / seaLevel);
    } else if (h < sandLevel) {
        finalColor = sand;
    } else if (h < grassLevel) {
        finalColor = mix(sand, grass, (h - sandLevel) / (grassLevel - sandLevel));
    } else if (h < rockLevel) {
        finalColor = mix(grass, rock, (h - grassLevel) / (rockLevel - grassLevel));
    } else if (h < snowLevel) {
        finalColor = rock;
    } else {
        finalColor = snow;
    }

    // Latitude based snow
    float lat = abs(lPos.y);
    if (lat > 0.8) {
        float snowFactor = smoothstep(0.8, 0.95, lat);
        finalColor = mix(finalColor, snow, snowFactor);
    }

    // Basic lighting
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(n, lightDir), 0.1);
    
    color = vec4(finalColor * diff, 1.0);
}
