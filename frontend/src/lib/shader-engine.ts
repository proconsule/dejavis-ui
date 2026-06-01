// src/lib/shader-engine.ts

export const WEBGL_PREFIX = `#version 300 es
precision highp float;

// Mappatura UBO ShadertoyUniforms
uniform vec3  iResolution;
uniform float iTime;
uniform float iTimeDelta;
uniform int   iFrame;
uniform vec4  iMouse;
uniform vec4  iDate;
uniform float iSampleRate;
uniform vec4  bassmidtreble; // x: bass, y: mid, z: treble, w: global

// Dati audio extra (i 512 campioni)
uniform float iWaveform[512];
uniform float iFFT[512];

out vec4 outColor;
`;

export const WEBGL_SUFFIX = `
void main() {
    vec4 color = vec4(0.0);
    mainImage(color, gl_FragCoord.xy);
    outColor = vec4(color.rgb, 1.0);
}`;

export function createShaderProgram(gl: WebGL2RenderingContext, fragmentSource: string) {
    const vsSource = `#version 300 es
    in vec4 position;
    void main() { gl_Position = position; }`;

    const compile = (type: number, src: string) => {
        const s = gl.createShader(type)!;
        gl.shaderSource(s, src);
        gl.compileShader(s);
        if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
            const log = gl.getShaderInfoLog(s);
            gl.deleteShader(s);
            // Risoluzione TS2769: usiamo l'operatore ?? per garantire string | undefined
            throw new Error(log ?? "Errore di compilazione shader sconosciuto");
        }
        return s;
    };

    const vShader = compile(gl.VERTEX_SHADER, vsSource);
    const fShader = compile(gl.FRAGMENT_SHADER, `${WEBGL_PREFIX}\n${fragmentSource}\n${WEBGL_SUFFIX}`);

    const prog = gl.createProgram()!;
    gl.attachShader(prog, vShader);
    gl.attachShader(prog, fShader);
    gl.linkProgram(prog);

    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
        const log = gl.getProgramInfoLog(prog);
        throw new Error(log ?? "Errore di linking del programma");
    }
    return prog;
}