// AUTOGENERATO — VideoFX shader sources
// Catena fissa: chromakey -> color -> mixer
//
// Entrambi gli shader operano alla risoluzione del frame (no scaling).
// I push constants sono "neutri" per default → effetto passthrough quando disabilitato.

#ifndef DEJAVIS_VIDEOFX_SHADER_SOURCES_H
#define DEJAVIS_VIDEOFX_SHADER_SOURCES_H

// =============================================================================
//  CHROMA KEY
//  Input:  sampler2D (texture sorgente, RGBA)
//  Output: storage image (RGBA con alpha modulato)
//
//  Algoritmo (color-distance based):
//    1. Calcola distanza nel colore (RGB) tra il pixel e il key color
//    2. Se distanza < threshold → alpha = 0 (trasparente)
//    3. Se distanza < threshold + softness → alpha sfumato (smoothstep)
//    4. Spill suppression: rimuovi il colore della chiave residuo nei contorni
//
//  Push constants:
//    keyR/G/B   = colore della chiave in [0..1]   (tipico verde 0.0/1.0/0.0 o 0.05/0.7/0.05)
//    threshold  = soglia di distanza             (0..1, tipico 0.1-0.3)
//    softness   = ampiezza della sfumatura       (0..1, tipico 0.05-0.2)
//    spill      = quanto sopprimere il verde     (0..1, tipico 0.5)
//    enabled    = flag (>0 attivo, <=0 passthrough)
// =============================================================================
static const char* videofx_chromakey_glsl = R"GLSL_END(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 0) uniform sampler2D inputSampled;
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D outputImage;

layout(push_constant) uniform PC {
    vec3  keyColor;      // RGB della chiave
    float threshold;     // 0..1
    float softness;      // 0..1
    float spill;         // 0..1
    float enabled;       // >0 attivo, <=0 passthrough
    float _pad;
} pc;

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz = imageSize(outputImage);
    if (p.x >= sz.x || p.y >= sz.y) return;

    vec2 uv = (vec2(p) + 0.5) / vec2(sz);
    vec4 c = texelFetch(inputSampled, p, 0);
    //vec4 c = texture(inputSampled, uv);

    if (pc.enabled <= 0.0) {
        imageStore(outputImage, p, c);
        return;
    }

    float d = distance(c.rgb, pc.keyColor);

    float alpha = smoothstep(pc.threshold, pc.threshold + pc.softness, d);

    if (pc.spill > 0.0) {
        float gExcess = c.g - max(c.r, c.b);
        if (gExcess > 0.0) {
            c.g -= gExcess * pc.spill;
        }
    }

    imageStore(outputImage, p, vec4(c.rgb, c.a * alpha));
}
)GLSL_END";


// =============================================================================
//  LUMA KEY
//  Input:  sampler2D (texture sorgente, RGBA)
//  Output: storage image (RGBA con alpha modulato)
//
//  Algoritmo:
//    1. Calcola luminanza del pixel (BT.601: 0.299R + 0.587G + 0.114B)
//    2. Se invert: luma = 1 - luma  (per chiave su luci invece che ombre)
//    3. alpha sfumato tra `lower` e `lower + softness` (smoothstep)
//    4. Se `upper` < 1: alpha cap superiore (banda passante)
//
//  Casi d'uso tipici:
//    - Rimuovi sfondo nero:    lower=0.05, upper=1.0,  softness=0.05, invert=0
//    - Rimuovi sfondo bianco:  lower=0.05, upper=1.0,  softness=0.05, invert=1
//    - Banda passante (mids):  lower=0.2,  upper=0.7,  softness=0.05, invert=0
//
//  Push constants (20 byte, layout lineare):
//    lower    = 0..1   soglia inferiore
//    upper    = 0..1   soglia superiore (>= lower+softness per attivare il cap)
//    invert   = 0 o 1
//    softness = 0..1   ampiezza banda di sfumatura
//    enabled  = >0 attivo, <=0 passthrough
// =============================================================================
static const char* videofx_lumakey_glsl = R"GLSL_END(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 0) uniform sampler2D inputSampled;
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D outputImage;

// Layout lineare std430: 5 float consecutivi, totale 20 byte.
layout(push_constant) uniform PC {
    float lower;
    float upper;
    float invert;
    float softness;
    float enabled;
} pc;

void main() {
    ivec2 p  = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz = imageSize(outputImage);
    if (p.x >= sz.x || p.y >= sz.y) return;

    vec2 uv = (vec2(p) + 0.5) / vec2(sz);
    vec4 c  = texture(inputSampled, uv);

    if (pc.enabled <= 0.0) {
        imageStore(outputImage, p, c);
        return;
    }

    float softness = max(pc.softness, 1e-4);

    float luma = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    if (pc.invert > 0.5) luma = 1.0 - luma;

    // Alpha = 0 sotto `lower`, 1 sopra `lower + softness`, sfumato in mezzo.
    float alpha = smoothstep(pc.lower, pc.lower + softness, luma);

    // Cap superiore opzionale: alpha torna a 0 sopra `upper`.
    if (pc.upper > pc.lower + softness && pc.upper < 1.0) {
        float upperAlpha = 1.0 - smoothstep(pc.upper, pc.upper + softness, luma);
        alpha = min(alpha, upperAlpha);
    }

    imageStore(outputImage, p, vec4(c.rgb, c.a * alpha));
}
)GLSL_END";


// =============================================================================
//  COLOR PROCESSING
//  Input:  storage image (output del chromakey, RGBA)
//  Output: storage image (RGBA finale, mostrata al mixer)
//
//  Pipeline operazioni nell'ordine standard:
//    1. Black/white levels (espansione/compressione del range)
//    2. Brightness (offset lineare)
//    3. Contrast (gain attorno a 0.5)
//    4. Gamma (correzione non lineare)
//    5. Saturation (interpolazione tra grayscale e colore)
//    6. Hue shift (rotazione nel piano YIQ)
//
//  Push constants (tutti neutri = passthrough):
//    brightness   = -1..1   (0 = neutro)
//    contrast     =  0..2   (1 = neutro)
//    saturation   =  0..2   (1 = neutro)
//    gamma        =  0.4..2.5 (1 = neutro)
//    hueShift     = -180..180 gradi (0 = neutro)
//    blackLevel   =  0..0.2 (0 = nessuno)
//    whiteLevel   =  0.8..1.0 (1 = nessuno)
//    enabled      = >0 attivo, <=0 passthrough
// =============================================================================
static const char* videofx_color_glsl = R"GLSL_END(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 0, rgba8) uniform readonly  image2D inputImage;
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D outputImage;

layout(push_constant) uniform PC {
    float brightness;
    float contrast;
    float saturation;
    float gamma;
    float hueShift;     // gradi
    float blackLevel;
    float whiteLevel;
    float enabled;
} pc;

vec3 applyHueShift(vec3 c, float angleDeg) {
    if (abs(angleDeg) < 0.001) return c;
    float a = radians(angleDeg);
    float s = sin(a);
    float co = cos(a);
    // Matrice di rotazione hue in spazio YIQ (approssimazione standard)
    mat3 m = mat3(
        0.299 + 0.701*co + 0.168*s,  0.587 - 0.587*co + 0.330*s,  0.114 - 0.114*co - 0.497*s,
        0.299 - 0.299*co - 0.328*s,  0.587 + 0.413*co + 0.035*s,  0.114 - 0.114*co + 0.292*s,
        0.299 - 0.300*co + 1.250*s,  0.587 - 0.588*co - 1.050*s,  0.114 + 0.886*co - 0.203*s
    );
    return clamp(m * c, 0.0, 1.0);
}

void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz = imageSize(outputImage);
    if (p.x >= sz.x || p.y >= sz.y) return;

    vec4 c = imageLoad(inputImage, p);

    if (pc.enabled <= 0.0) {
        imageStore(outputImage, p, c);
        return;
    }

    vec3 col = c.rgb;

    // 1. Black/white level — comprimi il range
    if (pc.whiteLevel > pc.blackLevel) {
        col = (col - pc.blackLevel) / max(pc.whiteLevel - pc.blackLevel, 0.001);
        col = clamp(col, 0.0, 1.0);
    }

    // 2. Brightness — offset
    col += pc.brightness;

    // 3. Contrast — pivot 0.5
    col = (col - 0.5) * pc.contrast + 0.5;

    col = clamp(col, 0.0, 1.0);

    // 4. Gamma
    if (abs(pc.gamma - 1.0) > 0.001) {
        col = pow(col, vec3(1.0 / pc.gamma));
    }

    // 5. Saturation
    if (abs(pc.saturation - 1.0) > 0.001) {
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = mix(vec3(lum), col, pc.saturation);
    }

    // 6. Hue shift
    col = applyHueShift(col, pc.hueShift);

    col = clamp(col, 0.0, 1.0);

    imageStore(outputImage, p, vec4(col, c.a));
}
)GLSL_END";

#endif // DEJAVIS_VIDEOFX_SHADER_SOURCES_H