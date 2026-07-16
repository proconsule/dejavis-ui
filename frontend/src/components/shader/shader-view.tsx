import { useEffect, useRef } from 'react';
import { createShaderProgram } from '@/lib/shader-engine.ts';

interface ShaderViewProps {
    compiledCode: string;
    onError: (err: string | null) => void;
    onStats?: (stats: { time: number; frame: number; fps: number }) => void;
}

export function ShaderView({ compiledCode, onError, onStats }: ShaderViewProps) {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const programRef = useRef<WebGLProgram | null>(null);
    const requestRef = useRef<number | null>(null);

    const frameCountRef = useRef(0);
    const lastTimeRef = useRef(0);
    const lastFpsUpdateRef = useRef(0);
    const framesSinceLastUpdate = useRef(0);
    const lastFrameTimestampRef = useRef(0);


    useEffect(() => {
        const canvas = canvasRef.current!;
        const gl = canvas.getContext('webgl2', { antialias: true })!;

        try {
            const newProgram = createShaderProgram(gl, compiledCode);
            if (programRef.current) gl.deleteProgram(programRef.current);
            programRef.current = newProgram;
            onError(null);
            frameCountRef.current = 0;
        } catch (e: any) {
            onError(e.message);
            return;
        }

        const posLoc = gl.getAttribLocation(programRef.current, "position");
        const buffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1, 1,-1, -1,1, -1,1, 1,-1, 1,1]), gl.STATIC_DRAW);

        const texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([255, 255, 255  , 255]));

        const image = new Image();
        image.src = 'dejavisui-logo.png'; // Sostituisci con il path della tua immagine
        image.onload = () => {
            gl.bindTexture(gl.TEXTURE_2D, texture);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
            gl.generateMipmap(gl.TEXTURE_2D);
        };

        const render = (time: number) => {
            if (!programRef.current) return;

            const targetFPS = 60;
            const frameDuration = 1000 / targetFPS;
            const elapsed = time - lastFrameTimestampRef.current;

            if (elapsed < frameDuration) {
                requestRef.current = requestAnimationFrame(render);
                return;
            }
            lastFrameTimestampRef.current = time - (elapsed % frameDuration);// -----------------------------


            const currentTime = time * 0.001;
            const deltaTime = currentTime - lastTimeRef.current;
            lastTimeRef.current = currentTime;
            frameCountRef.current++;
            framesSinceLastUpdate.current++;

            if (time - lastFpsUpdateRef.current > 500) {
                const fps = Math.round((framesSinceLastUpdate.current * 1000) / (time - lastFpsUpdateRef.current));
                onStats?.({ time: currentTime, frame: frameCountRef.current, fps: fps });
                framesSinceLastUpdate.current = 0;
                lastFpsUpdateRef.current = time;
            }

            gl.viewport(0, 0, canvas.width, canvas.height);
            gl.useProgram(programRef.current);

            gl.uniform3f(gl.getUniformLocation(programRef.current, "iResolution"), canvas.width, canvas.height, 1);
            gl.uniform1f(gl.getUniformLocation(programRef.current, "iTime"), currentTime);
            gl.uniform1f(gl.getUniformLocation(programRef.current, "iTimeDelta"), deltaTime);
            gl.uniform1i(gl.getUniformLocation(programRef.current, "iFrame"), frameCountRef.current);
            gl.uniform1f(gl.getUniformLocation(programRef.current, "iSampleRate"), 44100.0);


            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, texture);
            gl.uniform1i(gl.getUniformLocation(programRef.current, "iChannel0"), 0);

            //const audioLoc = gl.getUniformLocation(programRef.current, "bassmidtreble");


            gl.enableVertexAttribArray(posLoc);
            gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
            gl.drawArrays(gl.TRIANGLES, 0, 6);

            requestRef.current = requestAnimationFrame(render);
        };

        requestRef.current = requestAnimationFrame(render);

        return () => {
            if (requestRef.current) cancelAnimationFrame(requestRef.current);
            if (programRef.current) gl.deleteProgram(programRef.current);
            gl.deleteBuffer(buffer);
        };
    }, [compiledCode]);

    return (
        <canvas
            ref={canvasRef}
            className="w-full h-full bg-black block shadow-2xl"
            width={1280}
            height={720}
        />
    );
}