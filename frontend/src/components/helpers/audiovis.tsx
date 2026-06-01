import React, { useRef, useEffect } from 'react';

const VS_SOURCE = `
    attribute vec2 position;
    void main() {
        gl_Position = vec4(position, 0.0, 1.0);
    }
`;

const FS_SOURCE = `
    precision mediump float;
    uniform float u_fft[16];
    uniform vec2 u_resolution;

    void main() {
        vec2 uv = gl_FragCoord.xy / u_resolution;
        
        // 1. Identificazione barra
        int index = int(uv.x * 16.0);
        float value = 0.0;
        for (int i = 0; i < 16; i++) {
            if (i == index) {
                value = u_fft[i];
                break;
            }
        }

        // 2. Maschera Orizzontale (spazio tra le barre)
        float gapX = 0.15; 
        float localX = fract(uv.x * 16.0);
        float maskX = step(gapX, localX) * step(gapX, 1.0 - localX);

        // 3. Maschera Verticale (altezza della barra solida)
        // Usiamo smoothstep per un bordo leggermente meno "tagliente" se preferisci, 
        // ma step è il più fedele al look digitale.
        float fillMask = step(uv.y, value);

        // 4. Logica Colore Dinamica (Verde -> Giallo -> Rosso)
        vec3 color;
        if (uv.y < 0.6) {
            // Zona Sicura: Verde smeraldo
            color = vec3(0.0, 0.8, 0.4);
        } else if (uv.y < 0.85) {
            // Zona Warning: Giallo ambra
            color = vec3(1.0, 0.7, 0.0);
        } else {
            // Zona Peak: Rosso alert
            color = vec3(1.0, 0.2, 0.2);
        }

        // Background scuro per le zone vuote delle barre
        vec3 bgBar = vec3(0.1, 0.1, 0.12);
        
        // Composizione: se è sopra il valore 'value' mostra il bg scuro, altrimenti il colore
        vec3 finalColor = mix(bgBar, color, fillMask);

        // Applichiamo la maschera orizzontale (nero tra le barre)
        gl_FragColor = vec4(finalColor * maskX, maskX);
    }
`;

interface Props {
    fftRef: React.MutableRefObject<Float32Array>;
}

export const AudioVisualizerWebGL: React.FC<Props> = ({ fftRef }) => {
    const canvasRef = useRef<HTMLCanvasElement>(null);

    useEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;
        const gl = canvas.getContext('webgl');
        if (!gl) return;

        const createShader = (gl: WebGLRenderingContext, type: number, source: string) => {
            const shader = gl.createShader(type)!;
            gl.shaderSource(shader, source);
            gl.compileShader(shader);
            return shader;
        };

        const program = gl.createProgram()!;
        gl.attachShader(program, createShader(gl, gl.VERTEX_SHADER, VS_SOURCE));
        gl.attachShader(program, createShader(gl, gl.FRAGMENT_SHADER, FS_SOURCE));
        gl.linkProgram(program);
        gl.useProgram(program);

        const buffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1, 1,-1, -1,1, -1,1, 1,-1, 1,1]), gl.STATIC_DRAW);

        const posLoc = gl.getAttribLocation(program, "position");
        gl.enableVertexAttribArray(posLoc);
        gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);

        const fftLoc = gl.getUniformLocation(program, "u_fft");
        const resLoc = gl.getUniformLocation(program, "u_resolution");

        let animationFrameId: number;

        const render = () => {
            if (canvas.width !== canvas.clientWidth || canvas.height !== canvas.clientHeight) {
                canvas.width = canvas.clientWidth;
                canvas.height = canvas.clientHeight;
                gl.viewport(0, 0, canvas.width, canvas.height);
            }

            const currentData = (fftRef?.current?.length > 0) ? fftRef.current : new Float32Array(16).fill(0);

            gl.uniform1fv(fftLoc, currentData);
            gl.uniform2f(resLoc, canvas.width, canvas.height);
            gl.drawArrays(gl.TRIANGLES, 0, 6);
            animationFrameId = requestAnimationFrame(render);
        };

        render();
        return () => {
            cancelAnimationFrame(animationFrameId);
            gl.deleteProgram(program);
        };
    }, []);

    return (
        <canvas
            ref={canvasRef}
            // Altezza compatta (h-10 = 40px), perfetta per una barra di stato
            className="w-full h-10 rounded-md border border-slate-800/50 bg-black/20"
        />
    );
};