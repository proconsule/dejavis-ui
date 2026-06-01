"use client";

import React, { useRef, useEffect } from 'react';

interface Props {
    fftRef: React.RefObject<Float32Array>;
}

export const FFTCanvas: React.FC<Props> = ({ fftRef }) => {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const smoothedValues = useRef(new Float32Array(16));

    useEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;
        const ctx = canvas.getContext('2d', { alpha: false });
        if (!ctx) return;

        let raf: number;
        let lastTime = 0;
        const interval = 1000 / 60; // 60 FPS Target

        const RELEASE = 0.2;

        const render = (currentTime: number) => {
            raf = requestAnimationFrame(render);

            const delta = currentTime - lastTime;
            if (delta < interval) return;
            lastTime = currentTime - (delta % interval);

            if (canvas.width !== canvas.clientWidth || canvas.height !== canvas.clientHeight) {
                canvas.width = canvas.clientWidth;
                canvas.height = canvas.clientHeight;
            }

            const w = canvas.width;
            const h = canvas.height;
            const data = fftRef.current;

            if (data) {
                for (let i = 0; i < 16; i++) {
                    const target = data[i] || 0;
                    if (target > smoothedValues.current[i]) {
                        smoothedValues.current[i] = target;
                    } else {
                        smoothedValues.current[i] += (target - smoothedValues.current[i]) * RELEASE;
                    }
                }
            }

            // 2. Disegno Sfondo e Slot
            ctx.fillStyle = '#0f172a';
            ctx.fillRect(0, 0, w, h);

            const gap = 2;
            const barWidth = (w - (gap * 15)) / 16;

            const gradient = ctx.createLinearGradient(0, h, 0, 0);
            gradient.addColorStop(0, '#10b981');
            gradient.addColorStop(0.6, '#f59e0b');
            gradient.addColorStop(0.9, '#ef4444');

            // Disegno barre
            for (let i = 0; i < 16; i++) {
                const val = smoothedValues.current[i];
                const x = i * (barWidth + gap);

                ctx.fillStyle = '#1e293b'; // Slot vuoto
                ctx.fillRect(x, 0, barWidth, h);

                ctx.fillStyle = gradient; // Livello FFT
                ctx.fillRect(x, h - (val * h), barWidth, val * h);
            }

            // 3. DISEGNO TACCHE OTTIMIZZATO (Unico Path)
            ctx.strokeStyle = '#0f172a';
            ctx.lineWidth = 1;
            ctx.beginPath();
            for (let y = 0; y < h; y += 6) {
                ctx.moveTo(0, y);
                ctx.lineTo(w, y);
            }
            ctx.stroke(); // Esegue il disegno di tutte le linee in un colpo solo
        };

        raf = requestAnimationFrame(render);
        return () => cancelAnimationFrame(raf);
    }, [fftRef]);

    return (
        <canvas
            ref={canvasRef}
            className="w-full h-full block bg-black rounded shadow-inner"
        />
    );
};