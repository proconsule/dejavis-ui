import { useEffect, useRef } from 'react';
import { createShaderProgram } from '@/lib/shader-engine';

interface ShaderPreviewProps {
    compiledCode: string;
    onError: (err: string | null) => void;
}

export function ShaderPreview({ compiledCode, onError }: ShaderPreviewProps) {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const requestRef = useRef<number | null>(null);
    const programRef = useRef<WebGLProgram | null>(null);

    useEffect(() => {
        const canvas = canvasRef.current!;
        const gl = canvas.getContext('webgl2', { preserveDrawingBuffer: true })!;

        try {
            const newProgram = createShaderProgram(gl, compiledCode);
            if (programRef.current) gl.deleteProgram(programRef.current);
            programRef.current = newProgram;
            onError(null); // Reset degli errori
        } catch (e: any) {
            onError(e.message);
            return;
        }

        const posLoc = gl.getAttribLocation(programRef.current, "position");
        const buffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1, 1,-1, -1,1, -1,1, 1,-1, 1,1]), gl.STATIC_DRAW);

        const render = (time: number) => {
            if (!programRef.current || !canvas) return;

            gl.viewport(0, 0, canvas.width, canvas.height);
            gl.useProgram(programRef.current);
            gl.enableVertexAttribArray(posLoc);
            gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);

            gl.uniform3f(gl.getUniformLocation(programRef.current, "iResolution"), canvas.width, canvas.height, 1);
            gl.uniform1f(gl.getUniformLocation(programRef.current, "iTime"), time * 0.001);

            gl.drawArrays(gl.TRIANGLES, 0, 6);
            requestRef.current = requestAnimationFrame(render);
        };

        requestRef.current = requestAnimationFrame(render);

        return () => {
            if (requestRef.current) cancelAnimationFrame(requestRef.current);
            if (programRef.current) gl.deleteProgram(programRef.current);
            gl.deleteBuffer(buffer);
        };
    }, [compiledCode, onError]);

    return (
        <canvas
            ref={canvasRef}
            className="w-full h-full bg-black block"
            width={1280}
            height={720}
        />
    );
}