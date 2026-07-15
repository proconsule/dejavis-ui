#include "shadertoy_layer.h"
#include <iostream>
#include <shaderc/shaderc.hpp>
#include <cstring>


static const char* SHADERTOY_PREFIX = R"(
#version 450

// Binding 0: Dati di controllo e tempo (UBO)
layout(std140, binding = 0) uniform ShadertoyUniforms {
    vec3  iResolution;
    float _pad0;          // Padding dopo vec3 (allineamento a 16 byte)

    float iTime;          // 16
    float iTimeDelta;     // 20
    int   iFrame;         // 24
    float _pad1;          // Padding per allineare iMouse (vec4) a 32

    vec4  iMouse;         // 32
    vec4  iDate;          // 48

    float iSampleRate;    // 64
    vec4  bassmidtreble;          // Padding finale per chiudere il blocco a 80 byte
};

layout(binding = 1) uniform sampler2D iChannel0;
layout(binding = 2) uniform sampler2D iChannel1;
layout(binding = 3) uniform sampler2D iChannel2;
layout(binding = 4) uniform sampler2D iChannel3;


// Output obbligatorio per il fragment shader in Vulkan
layout(location = 0) out vec4 outColor;


// Dichiarazione della funzione che verrà implementata dall'utente
void mainImage(out vec4 fragColor, in vec2 fragCoord);


)";


static const char* SHADERTOY_SUFFIX = R"(
layout(location = 0) in vec2 outUV;

void main() {

    vec2 fragCoord = vec4(gl_FragCoord.x, gl_FragCoord.y, 0.0, 0.0).xy;

    vec4 fragColor = vec4(0.0, 0.0, 0.0, 1.0);

    // Eseguiamo lo shader
    mainImage(fragColor, fragCoord);

    outColor = vec4(clamp(fragColor.rgb, 0.0, 1.0), 1.0);
}
)";

/*

static const char* SHADERTOY_PREFIX = R"(
#version 450

// Binding 0: Dati di controllo e tempo (UBO)
layout(std140, binding = 0) uniform ShadertoyUniforms {
    vec3  iResolution;
    float _pad0;          // Padding dopo vec3 (allineamento a 16 byte)

    float iTime;          // 16
    float iTimeDelta;     // 20
    int   iFrame;         // 24
    float _pad1;          // Padding per allineare iMouse (vec4) a 32

    vec4  iMouse;         // 32
    vec4  iDate;          // 48

    float iSampleRate;    // 64
    vec4  bassmidtreble;          // Padding finale per chiudere il blocco a 80 byte
};


layout(binding = 1) uniform sampler2D iChannel0;
layout(binding = 2) uniform sampler2D iChannel1;
layout(binding = 3) uniform sampler2D iChannel2;
layout(binding = 4) uniform sampler2D iChannel3;

// Output obbligatorio per il fragment shader in Vulkan
layout(location = 0) out vec4 outColor;


// Dichiarazione della funzione che verrà implementata dall'utente
void mainImage(out vec4 fragColor, in vec2 fragCoord);


)";


static const char* SHADERTOY_SUFFIX = R"(
layout(location = 0) in vec2 outUV;

void main() {
    // Usiamo gl_FragCoord per la massima precisione dei pixel (linee nitide)
    // Ma dobbiamo invertire la Y perché Vulkan è Y-down e Shadertoy è Y-up.
    // Usiamo iResolution.y che arriva dall'UBO.
    vec2 fragCoord = vec4(gl_FragCoord.x, gl_FragCoord.y, 0.0, 0.0).xy;

    vec4 fragColor = vec4(0.0);

    // Eseguiamo lo shader
    mainImage(fragColor, fragCoord);

    // 1. Gamma correction: essenziale per eliminare l'effetto "piatto" e sgranato
    // 2. Clamp: evita che valori HDR rompano il colore finale
    vec3 gammaCorrected = pow(clamp(fragColor.rgb, 0.0, 1.0), vec3(1.0/2.2));

    outColor = vec4(gammaCorrected, fragColor.a);
}
)";

static const char* SHADERTOY_FALLBACK_FRAG = R"(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    fragColor = vec4(1.0, 0.0, 0.0, 1.0); // Magenta fisso per ora
}
)";

*/

static const char* SHADERTOY_TEST1_FRAG = R"(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Normalizziamo le coordinate (0.0 a 1.0) basandoci sulla risoluzione
    // Se iResolution non è ancora pronta, usiamo gl_FragCoord direttamente
    vec2 uv = fragCoord.xy / vec2(800.0, 600.0); // Valore fisso per il test

    // Creiamo un pattern colorato basato sulla posizione
    vec3 color = 0.5 + 0.5 * cos(uv.xyx + vec3(0, 2, 4));

    fragColor = vec4(color, 1.0);
}
)";

static const char* SHADERTOY_TEST2_FRAG = R"(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {

    vec3 col = vec3(abs(sin(iTime)), 0.0, 0.0);
    fragColor = vec4(col, 1.0);
}
)";

static const char* SHADERTOY_TEST3_FRAG = R"(
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    // Leggiamo la riga 0 della texture (FFT)
    // Se non hai ancora caricato dati, 'audio' sarà probabilmente 0.0 (nero)
    float audio = texture(iChannel0, vec2(uv.x, 0.25)).r;

    // Uniamo il tempo che già funziona con un feedback visuale della texture
    vec3 col = vec3(audio, abs(sin(iTime)) * 0.5, uv.x);

    fragColor = vec4(col, 1.0);
}
)";

static const char* SHADERTOY_TEST4_FRAG = R"(

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    // Campioniamo il canale Sinistro (riga 0)
    float left  = texture(iChannel0, vec2(uv.x, 0.25)).r;
    // Campioniamo il canale Destro (riga 1)
    float right = texture(iChannel0, vec2(uv.x, 0.75)).r;

    // Visualizziamo due barre: una verde per Left, una blu per Right
    vec3 col = vec3(0.0);

    // Barra superiore per il canale sinistro
    if (uv.y > 0.5) {
        col = vec3(0.0, left, 0.0);
    }
        // Barra inferiore per il canale destro
    else {
        col = vec3(0.0, 0.0, right);
    }

    fragColor = vec4(col, 1.0);
}
)";

static const char* SHADERTOY_TEST5_FRAG = R"(
// afl_ext 2017-2024
// MIT License

// Use your mouse to move the camera around! Press the Left Mouse Button on the image to look around!

#define DRAG_MULT 0.38 // changes how much waves pull on the water
#define WATER_DEPTH 1.0 // how deep is the water
#define CAMERA_HEIGHT 1.5 // how high the camera should be
#define ITERATIONS_RAYMARCH 12 // waves iterations of raymarching
#define ITERATIONS_NORMAL 36 // waves iterations when calculating normals

#define NormalizedMouse (iMouse.xy / iResolution.xy) // normalize mouse coords

// Calculates wave value and its derivative,
// for the wave direction, position in space, wave frequency and time
vec2 wavedx(vec2 position, vec2 direction, float frequency, float timeshift) {
  float x = dot(direction, position) * frequency + timeshift;
  float wave = exp(sin(x) - 1.0);
  float dx = wave * cos(x);
  return vec2(wave, -dx);
}

// Calculates waves by summing octaves of various waves with various parameters
float getwaves(vec2 position, int iterations) {
  float wavePhaseShift = length(position) * 0.1; // this is to avoid every octave having exactly the same phase everywhere
  float iter = 0.0; // this will help generating well distributed wave directions
  float frequency = 1.0; // frequency of the wave, this will change every iteration
  float timeMultiplier = 2.0; // time multiplier for the wave, this will change every iteration
  float weight = 1.0;// weight in final sum for the wave, this will change every iteration
  float sumOfValues = 0.0; // will store final sum of values
  float sumOfWeights = 0.0; // will store final sum of weights
  for(int i=0; i < iterations; i++) {
    // generate some wave direction that looks kind of random
    vec2 p = vec2(sin(iter), cos(iter));

    // calculate wave data
    vec2 res = wavedx(position, p, frequency, iTime * timeMultiplier + wavePhaseShift);

    // shift position around according to wave drag and derivative of the wave
    position += p * res.y * weight * DRAG_MULT;

    // add the results to sums
    sumOfValues += res.x * weight;
    sumOfWeights += weight;

    // modify next octave ;
    weight = mix(weight, 0.0, 0.2);
    frequency *= 1.18;
    timeMultiplier *= 1.07;

    // add some kind of random value to make next wave look random too
    iter += 1232.399963;
  }
  // calculate and return
  return sumOfValues / sumOfWeights;
}

// Raymarches the ray from top water layer boundary to low water layer boundary
float raymarchwater(vec3 camera, vec3 start, vec3 end, float depth) {
  vec3 pos = start;
  vec3 dir = normalize(end - start);
  for(int i=0; i < 64; i++) {
    // the height is from 0 to -depth
    float height = getwaves(pos.xz, ITERATIONS_RAYMARCH) * depth - depth;
    // if the waves height almost nearly matches the ray height, assume its a hit and return the hit distance
    if(height + 0.01 > pos.y) {
      return distance(pos, camera);
    }
    // iterate forwards according to the height mismatch
    pos += dir * (pos.y - height);
  }
  // if hit was not registered, just assume hit the top layer,
  // this makes the raymarching faster and looks better at higher distances
  return distance(start, camera);
}

// Calculate normal at point by calculating the height at the pos and 2 additional points very close to pos
vec3 normal(vec2 pos, float e, float depth) {
  vec2 ex = vec2(e, 0);
  float H = getwaves(pos.xy, ITERATIONS_NORMAL) * depth;
  vec3 a = vec3(pos.x, H, pos.y);
  return normalize(
    cross(
      a - vec3(pos.x - e, getwaves(pos.xy - ex.xy, ITERATIONS_NORMAL) * depth, pos.y),
      a - vec3(pos.x, getwaves(pos.xy + ex.yx, ITERATIONS_NORMAL) * depth, pos.y + e)
    )
  );
}

// Helper function generating a rotation matrix around the axis by the angle
mat3 createRotationMatrixAxisAngle(vec3 axis, float angle) {
  float s = sin(angle);
  float c = cos(angle);
  float oc = 1.0 - c;
  return mat3(
    oc * axis.x * axis.x + c, oc * axis.x * axis.y - axis.z * s, oc * axis.z * axis.x + axis.y * s,
    oc * axis.x * axis.y + axis.z * s, oc * axis.y * axis.y + c, oc * axis.y * axis.z - axis.x * s,
    oc * axis.z * axis.x - axis.y * s, oc * axis.y * axis.z + axis.x * s, oc * axis.z * axis.z + c
  );
}

// Helper function that generates camera ray based on UV and mouse
vec3 getRay(vec2 fragCoord) {
  vec2 uv = ((fragCoord.xy / iResolution.xy) * 2.0 - 1.0) * vec2(iResolution.x / iResolution.y, 1.0);
  // for fisheye, uncomment following line and comment the next one
  //vec3 proj = normalize(vec3(uv.x, uv.y, 1.0) + vec3(uv.x, uv.y, -1.0) * pow(length(uv), 2.0) * 0.05);
  vec3 proj = normalize(vec3(uv.x, uv.y, 1.5));
  if(iResolution.x < 600.0) {
    return proj;
  }
  return createRotationMatrixAxisAngle(vec3(0.0, -1.0, 0.0), 3.0 * ((NormalizedMouse.x + 0.5) * 2.0 - 1.0))
    * createRotationMatrixAxisAngle(vec3(1.0, 0.0, 0.0), 0.5 + 1.5 * (((NormalizedMouse.y == 0.0 ? 0.27 : NormalizedMouse.y) * 1.0) * 2.0 - 1.0))
    * proj;
}

// Ray-Plane intersection checker
float intersectPlane(vec3 origin, vec3 direction, vec3 point, vec3 normal) {
  return clamp(dot(point - origin, normal) / dot(direction, normal), -1.0, 9991999.0);
}

// Some very barebones but fast atmosphere approximation
vec3 extra_cheap_atmosphere(vec3 raydir, vec3 sundir) {
  //sundir.y = max(sundir.y, -0.07);
  float special_trick = 1.0 / (raydir.y * 1.0 + 0.1);
  float special_trick2 = 1.0 / (sundir.y * 11.0 + 1.0);
  float raysundt = pow(abs(dot(sundir, raydir)), 2.0);
  float sundt = pow(max(0.0, dot(sundir, raydir)), 8.0);
  float mymie = sundt * special_trick * 0.2;
  vec3 suncolor = mix(vec3(1.0), max(vec3(0.0), vec3(1.0) - vec3(5.5, 13.0, 22.4) / 22.4), special_trick2);
  vec3 bluesky= vec3(5.5, 13.0, 22.4) / 22.4 * suncolor;
  vec3 bluesky2 = max(vec3(0.0), bluesky - vec3(5.5, 13.0, 22.4) * 0.002 * (special_trick + -6.0 * sundir.y * sundir.y));
  bluesky2 *= special_trick * (0.24 + raysundt * 0.24);
  return bluesky2 * (1.0 + 1.0 * pow(1.0 - raydir.y, 3.0));
}

// Calculate where the sun should be, it will be moving around the sky
vec3 getSunDirection() {
  return normalize(vec3(-0.0773502691896258 , 0.5 + sin(iTime * 0.2 + 2.6) * 0.45 , 0.5773502691896258));
}

// Get atmosphere color for given direction
vec3 getAtmosphere(vec3 dir) {
   return extra_cheap_atmosphere(dir, getSunDirection()) * 0.5;
}

// Get sun color for given direction
float getSun(vec3 dir) {
  return pow(max(0.0, dot(dir, getSunDirection())), 720.0) * 210.0;
}

// Great tonemapping function from my other shader: https://www.shadertoy.com/view/XsGfWV
vec3 aces_tonemap(vec3 color) {
  mat3 m1 = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
  );
  mat3 m2 = mat3(
    1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602
  );
  vec3 v = m1 * color;
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return pow(clamp(m2 * (a / b), 0.0, 1.0), vec3(1.0 / 2.2));
}

// Main
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
  // get the ray
  vec3 ray = getRay(fragCoord);
  if(ray.y >= 0.0) {
    // if ray.y is positive, render the sky
    vec3 C = getAtmosphere(ray) + getSun(ray);
    fragColor = vec4(aces_tonemap(C * 2.0),1.0);
    return;
  }

  // now ray.y must be negative, water must be hit
  // define water planes
  vec3 waterPlaneHigh = vec3(0.0, 0.0, 0.0);
  vec3 waterPlaneLow = vec3(0.0, -WATER_DEPTH, 0.0);

  // define ray origin, moving around
  vec3 origin = vec3(iTime * 0.2, CAMERA_HEIGHT, 1);

  // calculate intersections and reconstruct positions
  float highPlaneHit = intersectPlane(origin, ray, waterPlaneHigh, vec3(0.0, 1.0, 0.0));
  float lowPlaneHit = intersectPlane(origin, ray, waterPlaneLow, vec3(0.0, 1.0, 0.0));
  vec3 highHitPos = origin + ray * highPlaneHit;
  vec3 lowHitPos = origin + ray * lowPlaneHit;

  // raymatch water and reconstruct the hit pos
  float dist = raymarchwater(origin, highHitPos, lowHitPos, WATER_DEPTH);
  vec3 waterHitPos = origin + ray * dist;

  // calculate normal at the hit position
  vec3 N = normal(waterHitPos.xz, 0.01, WATER_DEPTH);

  // smooth the normal with distance to avoid disturbing high frequency noise
  N = mix(N, vec3(0.0, 1.0, 0.0), 0.8 * min(1.0, sqrt(dist*0.01) * 1.1));

  // calculate fresnel coefficient
  float fresnel = (0.04 + (1.0-0.04)*(pow(1.0 - max(0.0, dot(-N, ray)), 5.0)));

  // reflect the ray and make sure it bounces up
  vec3 R = normalize(reflect(ray, N));
  R.y = abs(R.y);

  // calculate the reflection and approximate subsurface scattering
  vec3 reflection = getAtmosphere(R) + getSun(R);
  vec3 scattering = vec3(0.0293, 0.0698, 0.1717) * 0.1 * (0.2 + (waterHitPos.y + WATER_DEPTH) / WATER_DEPTH);

  // return the combined result
  vec3 C = fresnel * reflection + scattering;
  fragColor = vec4(aces_tonemap(C * 2.0), 1.0);
}

)";

static  const char* SHADERTOY_TEST6_FRAG = R"(
#define time iTime

const float CAM_FAR = 20.0;
const vec3 BACKGROUND = vec3(0.1, 0.1, 0.13);
const int WATER_MARCH_ITERATIONS = 12;
const int WATER_NORMAL_ITERATIONS = 39;
const float PI = 3.14159265359;

const int NUM_PARTICLES = 20;
vec4 ppos[NUM_PARTICLES];

// calculated per fragment
vec3 artifactOffset;
mat3 artifactRotation;
vec3 artifactAxis;
float flicker;
vec3 camFwd;
vec3 camUp;

float rand(float n) {
    return fract(sin(n) * 43758.5453123);
}
float hash(float n) { return fract(sin(n) * 1e4); }
float noise(float x) {
    float i = floor(x);
    float f = fract(x);
    float u = f * f * (3.0 - 2.0 * f);
    return mix(hash(i), hash(i + 1.0), u);
}
mat4 viewMatrix (vec3 dir, vec3 up) {
    vec3 f = normalize(dir);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    return mat4(
        vec4( s,   0.0),
        vec4( u,   0.0),
        vec4(-f,   0.0),
        vec4( 0.0, 0.0, 0.0, 1)
    );
}
mat3 rotationAlign(vec3 d, vec3 z) {
    vec3  v = cross(z, d);
    float c = dot(z, d);
    float k = 1.0/(1.0+c);
    return mat3(v.x*v.x*k + c,     v.y*v.x*k - v.z,    v.z*v.x*k + v.y,
                v.x*v.y*k + v.z,   v.y*v.y*k + c,      v.z*v.y*k - v.x,
                v.x*v.z*k - v.y,   v.y*v.z*k + v.x,    v.z*v.z*k + c    );
}
float intersectPlane(vec3 origin, vec3 direction, vec3 point, vec3 normal) {
    return clamp(dot(point - origin, normal) / dot(direction, normal), -1.0, 9991999.0);
}
vec3 calcRay(vec2 uv, float fov, float aspect) {
    uv = uv * 2.0 - 1.0;
    float d = 1.0 / tan(radians(fov) * 0.5);
    return normalize(vec3(aspect * uv.x, uv.y, d));
}
vec2 getWave(vec2 position, vec2 dir, float speed, float frequency, float timeshift) {
    float x = dot(dir, position) * frequency + timeshift * speed;
    float wave = exp(sin(x) - 1.0);
    float dist = wave * cos(x);
    return vec2(wave, -dist);
}
float heightmap(vec2 worldPos, int iterations) {
    const float scale = 0.13;
    vec2 p = worldPos * scale;
    vec2 p2 = (artifactOffset.xz - vec2(0.0, 1.0)) * scale;
    float d = clamp(length(p2 - p) / 0.8, 0.0, 1.0);
    d = (1.0 - smoothstep(0.0, 1.0, d)) * 0.8;
    float angle     = 0.0;
    float freq      = 5.0;
    float speed     = 2.0;
    float weight    = 1.9;
    float wave      = 0.0;
    float waveScale = 0.0;
    vec2 dir;
    vec2 res;
    for (int i = 0; i < iterations; i++) {
        dir = vec2(cos(angle), sin(angle));
        res = getWave(p, dir, speed, freq, time);
        p += dir * res.y * weight * 0.05;
        wave += res.x * weight - d;
        angle += 12.0;
        waveScale += weight;
        weight = mix(weight, 0.0, 0.2);
        freq *= 1.18;
        speed *= 1.06;
    }
    return wave / waveScale;
}
vec3 waterNormal(vec2 p, float eps) {
    vec2 h = vec2(eps, 0.0);
    #define i WATER_NORMAL_ITERATIONS
    return normalize(vec3(heightmap(p - h.xy, i) - heightmap(p + h.xy, i),
                          2.0 * eps,
                          heightmap(p - h.yx, i) - heightmap(p + h.yx, i)));
}
float octahedron(vec3 p, float s) {
  p = abs(p);
  return (p.x+p.y+p.z-s)*0.57735027;
}
void artifact(vec3 p, inout float curDist, inout vec3 glowColor, inout int id) {
    p -= artifactOffset;
    p = artifactRotation * p;
    float dist = octahedron(p, 1.2);
    const float glowDist = 4.8;
    if (dist < glowDist) {
        float d = dist + rand(dist) * 1.7;
        glowColor += vec3(0.75, 0.55, 0.45) * clamp(1.0 - pow((d / glowDist), 5.0), 0.0, 1.0) * 0.035 * flicker; // glow
    }
    if (dist < curDist) {
        curDist = dist;
        id = 1;
    }
}
void particles(vec3 p, inout float curDist, inout vec3 glowColor, inout int id) {
    float t;
    float angle;
    float radius;
    float dist = CAM_FAR;
    const float glowDist = 0.2;
    for (int i = 0; i < NUM_PARTICLES; i++) {
        dist = length(p - ppos[i].xyz) - 0.005;
        if (dist < glowDist && false) {
            float d = dist + rand(dist) * 0.5;
            glowColor += clamp(1.0 - d / glowDist, 0.0, 1.0) * 0.005;
        }
        if (dist < curDist) {
            curDist = dist;
            id = 2;
        }
    }
}
float objects(vec3 p, inout vec3 glowColor, inout int objId) {
    float dist = CAM_FAR;
    artifact(p, dist, glowColor, objId);
    particles(p, dist, glowColor, objId);
    return dist;
}
float artifactDist(vec3 p) {
    p -= artifactOffset;
    p = artifactRotation * p;
    return octahedron(p, 1.2);
}
float objectsDist(vec3 p) {
    return artifactDist(p);
}
vec3 objectsNormal(vec3 p, float eps) {
    vec2 h = vec2(eps, 0);
    #define f artifactDist
    return normalize(vec3(f(p + h.xyy) - f(p - h.xyy),
                          f(p + h.yxy) - f(p - h.yxy),
                          f(p + h.yyx) - f(p - h.yyx)));
}
vec3 objectsColor(int id, vec3 normal, vec3 ray) {
    if (id == 1) { // artifact
        float l = dot(normal, normalize(vec3(0.0, 1.0, 0.5)));
        float hl = mix(0.8, 1.5, l * 0.5 + 0.5);
        return vec3(0.85, 0.65, 0.55) * hl * flicker;
    }
    if (id == 2) {
        return vec3(0.85, 0.65, 0.55) * 1.5;
    }
    return vec3(1.0, 1.0, 0.0); // shouldn't happen
}
void marchObjects(vec3 eye, vec3 ray, float wDepth, inout vec4 color) {
    float dist = 0.0;
    int id;
    vec3 rayPos = eye + ray * dist;
    vec3 c;
    float depth = CAM_FAR;
    vec3 glowColor = vec3(0.0);
    for (int i = 0; i < 100; i++) {
        dist = objects(rayPos, color.rgb, id);
        depth = distance(rayPos, eye);
        if (depth > wDepth) {
            break;
        }
        if (dist < 0.01) {
            vec3 normal = objectsNormal(rayPos, 0.01);
            color = vec4(objectsColor(id, normal, ray), depth);
            return;
        }
        rayPos += ray * dist;
    }
}
vec3 waterColor(vec3 ray, vec3 normal, vec3 p) {
    vec3 color = vec3(0.0);
    float fogDist = length(p - vec3(0.0, 0.0, -6.));
    float dist = 0.0;
    int objId = 0;
    vec3 refl = reflect(ray, normal);
    vec3 rayPos = p + refl * dist;
    vec3 dir = normalize(artifactOffset - p);
    if (length(p.xz - artifactOffset.xz) < 8.5 && dot(refl, dir) > -0.25) { // hacky but this way we aren't reflecting on every single fragment
        for (int i = 0; i < 40; i++) {
            dist = objects(rayPos, color, objId);
            if (dist < 0.01) {
                vec3 objNormal = objectsNormal(rayPos, 0.001);
                color = objectsColor(objId, objNormal, rayPos);
                break;
            }
            rayPos += refl * dist;
        }
    }
    float fresnel = (0.04 + 0.9 * (pow(1.0 - max(0.0, dot(-normal, ray)), 7.0)));
    vec3 lightOffset = artifactOffset - p;
    float d = length(lightOffset);
    const float r = 14.0;
    float atten = clamp(1.0 - (d*d) / (r*r), 0.0, 1.0);
    atten *= atten;
    vec3 point = vec3(0.75, 0.55, 0.45) * atten * (1.0 + fresnel) * 0.07;
    vec3 ambient = vec3(dot(normal, normalize(vec3(0.0, 1.0, 0.5)))) * max(fresnel, 0.06) * vec3(0.1, 0.5, 1.0) * 0.85;
    float fog = smoothstep(25.0, 6.0, fogDist) / (fogDist * 0.1);

    return color + (point + ambient) * fog;
}
void marchWater(vec3 eye, vec3 ray, inout vec4 color) {
    const vec3 planeNorm = vec3(0.0, 1.0, 0.0);
    const float depth = 3.0;
    float ceilDist = intersectPlane(eye, ray, vec3(0.0, 0.0, 0.0), planeNorm);
    vec3 normal = vec3(0.0);
    if (dot(planeNorm, ray) > -0.05) {
        normal = vec3(0.0);
        color = vec4(vec3(0.0), CAM_FAR);
        return;
    }
    float height = 0.0;
    vec3 rayPos = eye + ray * ceilDist;
    for (int i = 0; i < 80; i++) {
        height = heightmap(rayPos.xz, WATER_MARCH_ITERATIONS) * depth - depth;
        if (rayPos.y - height < 0.1) {
            color.w = distance(rayPos, eye);
            vec3 normPos = (eye + ray * color.w);
            normal = waterNormal(normPos.xz, 0.005);
            color.rgb = waterColor(ray, normal, normPos);
            return;
        }
        rayPos += ray * (rayPos.y - height);
    }
}
vec3 march(vec2 uv, vec3 camPos) {
    mat4 vm = viewMatrix(camFwd, camUp);
    vec3 ray = (vm * vec4(calcRay(uv, 80.0, iResolution.x / iResolution.y), 1.0)).xyz;
    vec4 color = vec4(BACKGROUND, CAM_FAR);
    vec3 waterColor;
    marchWater(camPos, ray, color);
    marchObjects(camPos, ray, color.w, color);
    return color.rgb;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;

    // simulate  particles
    float pR;
    float pA;
    float gen;
    float t;
    float loop;
    float height;
    vec4 p;
    const float emitR = 1.7;
    for (int i = 0; i < NUM_PARTICLES; i++) {
        t = time * 0.035 + float(i) * 0.07;
        gen = floor(t);
        loop = fract(t);
        pR = rand(gen + float(i)) * emitR+bassmidtreble.x;
        pA = rand(float(i)) * PI * 2.0;
        p.xz = vec2(cos(pA), sin(pA)) * pR + vec2(0.0, -5.2);
        height = mix(3.0, 2.3, (abs(pR) / emitR)+bassmidtreble.y);
        p.y = mix(-3.5, height, sqrt(loop));
        //p.w = cos(loop * PI * 2.0) * min(1.0, 1.0 - (loop / 0.9)); // not currently used :(
        ppos[i] = p;
    }

    // artifact animation
    t = time;
    float s = sin(t);
    float c = cos(t);
    artifactRotation = mat3x3(c,0,s,
                              0,1,0,
                             -s,0,c);
    artifactRotation *= rotationAlign(vec3(0.0, 1.0, 0.0), vec3(sin(t)*0.1 * 0.2, 1.0, cos(t) * 0.2 + 0.3));
    artifactOffset = vec3(sin(time) * 0.4, cos(time * 0.5) * 0.3 - 1.7, -6.);
    flicker = mix(1.0, 1.1, sin(time * 2.0) * 0.5 + 0.5) + noise(time * 4.0) * -0.1 + 0.05;

    // camera animation
    camFwd = vec3(0.0, 0.7 + noise(time * 0.8 + 4.0) * 0.08 - 0.04, 1.0);
    camUp = vec3(noise(time * 1.2) * 0.02 - 0.01, 1.0, 0.0);

    // scene
    vec3 color = march(uv, vec3(0.0, 1.9, 1.0));

    // vignette
    color -= (length(uv - 0.5) - 0.3) * 0.05;
    fragColor = vec4(color, 1.0);
}

)";

static const char* SHADERTOY_VSHADER = R"(
#version 450
layout(location = 0) out vec2 outUV;

void main() {
    // Genera UV: (0,0), (2,0), (0,2)
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

    // outUV deve essere (0,0) in basso a sinistra per Shadertoy.
    // Invertiamo la Y qui così il fragment shader riceve le coordinate giuste.
    outUV = vec2(uv.x, 1.0 - uv.y);

    // In Vulkan NDC:
    // x: -1 (sinistra) a +1 (destra)
    // y: -1 (alto) a +1 (basso)
    // Con uv.y = 0 (che per noi è il basso), gl_Position.y deve essere 1.0.
    // Con uv.y = 1 (che per noi è l'alto), gl_Position.y deve essere -1.0.
    gl_Position = vec4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
}
)";

CShaderToy::CShaderToy() {}

std::string CShaderToy::ShaderToy_Comapt_Shader(std::string shader_code){
    std::string fullGLSL = "";
    fullGLSL += SHADERTOY_PREFIX;
    fullGLSL += "\n// --- USER CODE START ---\n";
    fullGLSL += shader_code;
    fullGLSL += "\n// --- USER CODE END ---\n";
    fullGLSL += SHADERTOY_SUFFIX;
    return fullGLSL;
}

void CShaderToy::Init(VulkanContext * _ctx, std::string &frag_shader,int _slotid,int _w ,int _h, VulkanTexture * _targetIamge) {
    m_ctx = _ctx;
    outTexture = _targetIamge;
    slotid = _slotid;
    current_frag_shader = frag_shader;
    current_frag_shader = SHADERTOY_TEST5_FRAG;
    std::string fullGLSL = ShaderToy_Comapt_Shader(current_frag_shader);

    // 1. Creazione dell'UBO (Binding 0: iTime, iResolution, etc.)
    // Usiamo una funzione helper Vulkan_CreateBuffer (che dovresti avere nel progetto)
    Vulkan_CreateBuffer(m_ctx, sizeof(ShadertoyUBO),
                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        m_uboBuffer, m_uboMemory);


    if (m_uboMemory != VK_NULL_HANDLE) {
        vkMapMemory(m_ctx->device, m_uboMemory, 0, sizeof(ShadertoyUBO), 0, &m_uboMappedPtr);
    }

    CreateImageResources(outTexture,_w,_h);

    CreateDescriptorPool();

    VkDescriptorSetLayoutBinding bindings[5] = {};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    for(int i = 1; i <= 4; i++) {
        bindings[i] = { (uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    }

    VkDescriptorSetLayoutCreateInfo dsLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dsLayoutInfo.bindingCount = 5; // Ora sono 5
    dsLayoutInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(m_ctx->device, &dsLayoutInfo, nullptr, &m_descriptorLayout);


    spirv_vertex_constant = Vulkan_GLSL2SPIRV(SHADERTOY_VSHADER, shaderc_vertex_shader, "main_vert");
    auto fragSpirv = Vulkan_GLSL2SPIRV(fullGLSL.c_str(), shaderc_fragment_shader, "main_frag");


    SetupGraphicsPipeline(spirv_vertex_constant, fragSpirv);

    VkDescriptorSetLayoutBinding outBinding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo outLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    outLayoutInfo.bindingCount = 1;
    outLayoutInfo.pBindings = &outBinding;
    vkCreateDescriptorSetLayout(m_ctx->device, &outLayoutInfo, nullptr, &m_outputDescriptorLayout);

    VkDescriptorSetAllocateInfo outAllocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    outAllocInfo.descriptorPool = m_descriptorPool;
    outAllocInfo.descriptorSetCount = 1;
    outAllocInfo.pSetLayouts = &m_outputDescriptorLayout;
    vkAllocateDescriptorSets(m_ctx->device, &outAllocInfo, &m_outputDescriptorSet);

    // Update del set di output con la vista della texture appena creata
    VkDescriptorImageInfo outImageInfo = {};
    outImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // Assicurati che il render finale faccia il transition
    outImageInfo.imageView = outTexture->view;
    outImageInfo.sampler = m_ctx->defaultSampler; // O un sampler creato ad hoc

    VkWriteDescriptorSet outWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    outWrite.dstSet = m_outputDescriptorSet;
    outWrite.dstBinding = 0;
    outWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    outWrite.descriptorCount = 1;
    outWrite.pImageInfo = &outImageInfo;
    vkUpdateDescriptorSets(m_ctx->device, 1, &outWrite, 0, nullptr);



    AllocateAndUpdateInputDescriptorSet();

    std::vector<TextureBinding> activeTextures;
    BindResources(activeTextures);

}

/*

void CShaderToy::SetupGraphicsPipeline(const char* vertSource, const char* fragSource) {
    // --- 1. COMPILAZIONE ---
    auto vertSpirv = Vulkan_GLSL2SPIRV(vertSource, shaderc_vertex_shader, "main_vert");
    auto fragSpirv = Vulkan_GLSL2SPIRV(fragSource, shaderc_fragment_shader, "main_frag");

    DEJAVISUI_LOG_DEBUG("[SHADERTOY] SETUP GRAPH PIPELINE");


    if (vertSpirv.empty() || fragSpirv.empty()) {
        return;
    }
    VkShaderModule vertModule = Vulkan_CreateShader(m_ctx->device, vertSpirv);
    VkShaderModule fragModule = Vulkan_CreateShader(m_ctx->device, fragSpirv);


    DEJAVISUI_LOG_DEBUG("[SHADERTOY] SHADER COMPILED");


    // --- 3. PIPELINE LAYOUT ---
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorLayout;
    vkCreatePipelineLayout(m_ctx->device, &pipelineLayoutInfo, nullptr, &m_graphicsPipelineLayout);


    DEJAVISUI_LOG_DEBUG("[SHADERTOY] PIPELINE LAYOUT");

    // --- 4. CONFIGURAZIONE PIPELINE ---
    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr };
    shaderStages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr };

    // Stato Vertex Input (Vuoto per il full screen triangle)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport e Scissor Dinamici (per gestire resize finestra)
    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlend = {};
    colorBlend.colorWriteMask = 0xF; // RGBA
    colorBlend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlend;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynStates;



    // --- 5. CREAZIONE FINALE ---
    VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_graphicsPipelineLayout;
    pipelineInfo.renderPass = outTexture->renderPass;
    pipelineInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(m_ctx->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline);
    if (res != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Pipeline creation failed with result: %d", res);
    }


    DEJAVISUI_LOG_DEBUG("[SHADERTOY] PIPELINE OK");

    // Cleanup moduli
    vkDestroyShaderModule(m_ctx->device, fragModule, nullptr);
    vkDestroyShaderModule(m_ctx->device, vertModule, nullptr);
}

*/

void CShaderToy::SetupGraphicsPipeline(std::vector<uint32_t> _spirv_vertx , std::vector<uint32_t> _spirv_frag) {
    // --- 1. COMPILAZIONE ---
    //auto vertSpirv = Vulkan_GLSL2SPIRV(vertSource, shaderc_vertex_shader, "main_vert");
    //auto fragSpirv = Vulkan_GLSL2SPIRV(fragSource, shaderc_fragment_shader, "main_frag");

    DEJAVISUI_LOG_DEBUG("[SHADERTOY] SETUP GRAPH PIPELINE");


    if (_spirv_vertx.empty() || _spirv_frag.empty()) {
        return;
    }
    VkShaderModule vertModule = Vulkan_CreateShader(m_ctx->device, _spirv_vertx);
    VkShaderModule fragModule = Vulkan_CreateShader(m_ctx->device, _spirv_frag);


    DEJAVISUI_LOG_DEBUG("[SHADERTOY] SHADER COMPILED");


    // --- 3. PIPELINE LAYOUT ---
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorLayout;
    vkCreatePipelineLayout(m_ctx->device, &pipelineLayoutInfo, nullptr, &m_graphicsPipelineLayout);


    DEJAVISUI_LOG_DEBUG("[SHADERTOY] PIPELINE LAYOUT");

    // --- 4. CONFIGURAZIONE PIPELINE ---
    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    shaderStages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr };
    shaderStages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr };

    // Stato Vertex Input (Vuoto per il full screen triangle)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport e Scissor Dinamici (per gestire resize finestra)
    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlend = {};
    colorBlend.colorWriteMask = 0xF; // RGBA
    colorBlend.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlend;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynStates;



    // --- 5. CREAZIONE FINALE ---
    VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_graphicsPipelineLayout;
    pipelineInfo.renderPass = outTexture->renderPass;
    pipelineInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(m_ctx->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline);
    if (res != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("Pipeline creation failed with result: %d", res);
    }


    DEJAVISUI_LOG_DEBUG("[SHADERTOY] PIPELINE OK");

    // Cleanup moduli
    vkDestroyShaderModule(m_ctx->device, fragModule, nullptr);
    vkDestroyShaderModule(m_ctx->device, vertModule, nullptr);
}


void CShaderToy::Compute(VkCommandBuffer cmd, const FragShadersPushConstants &pc) {
    currentpc = pc;
    UpdateUBO();
}

void CShaderToy::BindAndDraw(VkCommandBuffer cmd) {

    if (m_graphicsPipeline == VK_NULL_HANDLE) return;

    for (const auto& binding : m_channels) {
        if (binding.texture.image != VK_NULL_HANDLE) {
            imageBarrier(cmd,
                         binding.texture.image,
                         VK_IMAGE_LAYOUT_GENERAL, // O il layout corrente della texture
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_WRITE_BIT,
                         VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    // 5. Viewport e Scissor Dinamici
    // Usiamo le dimensioni correnti della swapchain per evitare distorsioni
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(outTexture->width);
    viewport.height = static_cast<float>(outTexture->height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    VkExtent2D extent = {};
    extent.width = static_cast<float>(outTexture->width);
    extent.height = static_cast<float>(outTexture->height);
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (m_descriptorSet == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("[SHADERTOY] Attempted to bind NULL descriptor set!");
        return;
    }
    // 6. Bind Descriptor Sets
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_graphicsPipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    // 7. Draw (Full-screen Triangle)
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

VkDescriptorPool CShaderToy::Vulkan_CreateDescriptorPool(
        VulkanContext * ctx,
        uint32_t uniformCount,
        uint32_t samplerCount,
        uint32_t maxSets
) {
    std::vector<VkDescriptorPoolSize> poolSizes;

    if (uniformCount > 0) {
        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformCount });
    }
    if (samplerCount > 0) {
        poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, samplerCount });
    }

    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes    = poolSizes.data();
    poolInfo.maxSets       = maxSets;

    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(m_ctx->device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pool;
}

void CShaderToy::BindResources(const std::vector<TextureBinding>& textures) {
    if (m_descriptorSet == VK_NULL_HANDLE) return;

    UpdateDescriptorSet(m_descriptorSet, m_uboBuffer, textures);
}


void CShaderToy::UpdateDescriptorSet(
        VkDescriptorSet descriptorSet,
        VkBuffer uboBuffer,
        const std::vector<TextureBinding>& bindings
) {
    std::vector<VkWriteDescriptorSet> descriptorWrites;

    // 1. Binding 0: Sempre l'UBO (Dati di sistema: iTime, iResolution, etc.)
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = uboBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range  = sizeof(ShadertoyUBO);

    VkWriteDescriptorSet uboWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    uboWrite.dstSet          = descriptorSet;
    uboWrite.dstBinding      = 0;
    uboWrite.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.descriptorCount = 1;
    uboWrite.pBufferInfo     = &bufferInfo;
    descriptorWrites.push_back(uboWrite);

    std::vector<VkDescriptorImageInfo> imageInfos(bindings.size());

    for (size_t i = 0; i < bindings.size(); i++) {
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[i].imageView   = bindings[i].texture.view;
        imageInfos[i].sampler     = bindings[i].sampler; // Qui usiamo il sampler specifico della struct

        VkWriteDescriptorSet texWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        texWrite.dstSet          = descriptorSet;
        texWrite.dstBinding      = static_cast<uint32_t>(i + 1); // Parte da binding 1 (iChannel0)
        texWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texWrite.descriptorCount = 1;
        texWrite.pImageInfo      = &imageInfos[i];
        descriptorWrites.push_back(texWrite);
    }

    vkUpdateDescriptorSets(m_ctx->device,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
}

void CShaderToy::AllocateAndUpdateInputDescriptorSet() {
    if (m_descriptorLayout == VK_NULL_HANDLE) {
        DEJAVISUI_LOG_ERROR("[SHADERTOY] Descriptor Layout is NULL!");
        return;
    }

    VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorLayout;

    if (vkAllocateDescriptorSets(m_ctx->device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        DEJAVISUI_LOG_ERROR("[SHADERTOY] Failed to allocate descriptor set");
        return;
    }

    DEJAVISUI_LOG_DEBUG("[SHADERTOY] Descriptor Set successfully allocated: %p", m_descriptorSet);


    std::vector<TextureBinding> emptyTextures;
    UpdateDescriptorSet(m_descriptorSet, m_uboBuffer, emptyTextures);
}

void CShaderToy::UpdateUBO() {

    m_uboData.iResolution[0] = static_cast<float>(outTexture->width);
    m_uboData.iResolution[1] = static_cast<float>(outTexture->height);
    m_uboData.iResolution[2] = 1.0f;

    m_uboData.iTimeDelta = currentpc.time- m_uboData.iTime;
    m_uboData.iTime = currentpc.time;
    m_uboData.iFrame++;
    m_uboData.iMouse[0] = 0.0f;
    m_uboData.iMouse[1] = 0.0f;
    m_uboData.iMouse[2] = 0.0f;
    m_uboData.iMouse[3] = 0.0f;
    m_uboData.iSampleRate = 44100.0f;

    if (m_uboMappedPtr) {
        memcpy(m_uboMappedPtr, &m_uboData, sizeof(ShadertoyUBO));
    }
    prev_currentpc = currentpc;
}


bool CShaderToy::TestShader(std::string _frag_shader) {


    std::string fullGLSL = ShaderToy_Comapt_Shader(_frag_shader);
    auto newFragSpirv = Vulkan_GLSL2SPIRV(fullGLSL.c_str(), shaderc_fragment_shader, "main_frag");

    if (newFragSpirv.empty()) {
        return false;
    }

    return true;
}


bool CShaderToy::UpdateShader(std::vector<uint32_t> _spirvcode){

    //std::string fullGLSL = ShaderToy_Comapt_Shader(fragSource);
    //current_frag_shader = fragSource;
    //auto newFragSpirv = Vulkan_GLSL2SPIRV(fullGLSL.c_str(), shaderc_fragment_shader, "main_frag");

    if (_spirvcode.empty()) {
        return false;
    }

    vkDeviceWaitIdle(m_ctx->device);

    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_ctx->device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_graphicsPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_ctx->device, m_graphicsPipelineLayout, nullptr);
        m_graphicsPipelineLayout = VK_NULL_HANDLE;
    }

    SetupGraphicsPipeline(spirv_vertex_constant, _spirvcode);

    return true;
}


/*
bool CShaderToy::UpdateShader(const std::string& fragSource) {

    std::string fullGLSL = ShaderToy_Comapt_Shader(fragSource);
    current_frag_shader = fragSource;
    auto newFragSpirv = Vulkan_GLSL2SPIRV(fullGLSL.c_str(), shaderc_fragment_shader, "main_frag");

    if (newFragSpirv.empty()) {
        return false;
    }

    vkDeviceWaitIdle(m_ctx->device);

    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_ctx->device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_graphicsPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_ctx->device, m_graphicsPipelineLayout, nullptr);
        m_graphicsPipelineLayout = VK_NULL_HANDLE;
    }

    SetupGraphicsPipeline(SHADERTOY_VSHADER, fullGLSL.c_str());

    return true;
}
*/

void CShaderToy::Cleanup() {
    if (m_ctx && m_ctx->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_ctx->device);
        if (m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_ctx->device, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }

        if (m_graphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_ctx->device, m_graphicsPipeline, nullptr);
            m_graphicsPipeline = VK_NULL_HANDLE;
        }
        if (m_graphicsPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_ctx->device, m_graphicsPipelineLayout, nullptr);
            m_graphicsPipelineLayout = VK_NULL_HANDLE;
        }

        if (m_descriptorLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(m_ctx->device, m_descriptorLayout, nullptr);
            m_descriptorLayout = VK_NULL_HANDLE;
        }

        if (m_uboBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_ctx->device, m_uboBuffer, nullptr);
            vkFreeMemory(m_ctx->device, m_uboMemory, nullptr);
        }
    }
}

void CShaderToy::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[2] = {};

    // 1. Per l'UBO (iTime, iResolution...)
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 10;

    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 10;

    if (vkCreateDescriptorPool(m_ctx->device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        printf("Errore critico: Impossibile creare Descriptor Pool per FragShader\n");
    }
}

void CShaderToy::imageBarrier(VkCommandBuffer cmd, VkImage img,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout           = oldLayout;
    b.newLayout           = newLayout;
    b.srcAccessMask       = srcAccess;
    b.dstAccessMask       = dstAccess;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

bool CShaderToy::CreateImageResources(VulkanTexture* out, uint32_t w, uint32_t h) {
    if (!out) return false;

    if (w == 0 || h == 0) {
        w = 1280; h = 720;
    }

    out->width  = w;
    out->height = h;

    VkExternalMemoryImageCreateInfo externalInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
#ifdef _WIN32
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#endif

    VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent        = { w, h, 1 };
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_STORAGE_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.pNext         = &externalInfo;

    if (vkCreateImage(m_ctx->device, &imageInfo, nullptr, &out->image) != VK_SUCCESS) return false;

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_ctx->device, out->image, &memReqs);

    VkMemoryDedicatedAllocateInfo dedicatedInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicatedInfo.image = out->image;
    dedicatedInfo.pNext = nullptr;

#ifdef _WIN32
    VkExportMemoryAllocateInfo exportInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    exportInfo.pNext = &dedicatedInfo;
    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.pNext           = &exportInfo;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(m_ctx,memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
#else
    VkExportMemoryAllocateInfo exportInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    exportInfo.pNext = &dedicatedInfo;
    VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.pNext           = &exportInfo;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(m_ctx,memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
#endif
    if (vkAllocateMemory(m_ctx->device, &allocInfo, nullptr, &out->memory) != VK_SUCCESS) return false;
    vkBindImageMemory(m_ctx->device, out->image, out->memory, 0);

    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format         = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    std::array<VkSubpassDependency, 2> dependencies;
    dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass      = 0;
    dependencies[0].srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask   = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass      = 0;
    dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &colorAttachment;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    rpInfo.pDependencies   = dependencies.data();

    if (vkCreateRenderPass(m_ctx->device, &rpInfo, nullptr, &out->renderPass) != VK_SUCCESS) return false;

    VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image            = out->image;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(m_ctx->device, &viewInfo, nullptr, &out->view) != VK_SUCCESS) return false;

    VkFramebufferCreateInfo fbInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fbInfo.renderPass      = out->renderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments    = &out->view;
    fbInfo.width           = w;
    fbInfo.height          = h;
    fbInfo.layers          = 1;

    if (vkCreateFramebuffer(m_ctx->device, &fbInfo, nullptr, &out->framebuffer) != VK_SUCCESS) return false;

    DEJAVISUI_LOG_DEBUG("[VULKAN] SHaderToy Image create: %dx%d (RP: %p, IMG: %p)",
                        w, h, (void*)out->renderPass, (void*)out->image);
    return true;
}


void CShaderToy::SetiChannelTexture(VulkanUniTexture * _texture, int chanidx) {
    if (chanidx < 0 || chanidx >= 4) return;
    if (!_texture) return;

    m_channels[chanidx].texture.view = _texture->VkTexture.view;
    m_channels[chanidx].texture.image = _texture->VkTexture.image;
    m_channels[chanidx].sampler = m_ctx->defaultSampler;

    if (m_descriptorSet != VK_NULL_HANDLE) {
        UpdateDescriptorSet(m_descriptorSet, m_uboBuffer, m_channels);
    }

    DEJAVISUI_LOG_DEBUG("[SHADERTOY] Channel %d set to texture %p", chanidx, _texture);
}

void CShaderToy::DestroyImageResources(MasterResources* out) {
    if (!out) return;

    if (out->framebuffer) { vkDestroyFramebuffer(m_ctx->device, out->framebuffer, nullptr); out->framebuffer = VK_NULL_HANDLE; }
    if (out->image.view)        { vkDestroyImageView(m_ctx->device, out->image.view, nullptr);          out->image.view        = VK_NULL_HANDLE; }
    if (out->renderPass)  { vkDestroyRenderPass(m_ctx->device, out->renderPass, nullptr);   out->renderPass  = VK_NULL_HANDLE; }
    if (out->image.image)       { vkDestroyImage(m_ctx->device, out->image.image, nullptr);             out->image.image       = VK_NULL_HANDLE; }
    if (out->image.memory)      { vkFreeMemory(m_ctx->device, out->image.memory, nullptr);              out->image.memory      = VK_NULL_HANDLE; }

    out->image.width = 0;
    out->image.height = 0;
}