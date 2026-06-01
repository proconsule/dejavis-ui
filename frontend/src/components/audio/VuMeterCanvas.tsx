"use client";

import React, { useRef, useEffect } from 'react';

interface Props {
    levels: Float32Array;
}

const MIN_DB = -60;
const MAX_DB = 12;
const RANGE_DB = MAX_DB - MIN_DB;                  // 66
const ZERO_DB_POS = -MIN_DB / RANGE_DB;            // ≈ 0.909



export const VuMeterCanvas: React.FC<Props> = ({ levels }) => {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const smoothedLevels = useRef(new Float32Array([0, 0]));

    useEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;
        const ctx = canvas.getContext('2d', { alpha: false });
        if (!ctx) return;

        let raf: number;
        let lastTime = 0;
        const interval = 1000 / 60; // Target 60 FPS

        const ATTACK = 0.35;
        const RELEASE = 0.12;

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

            // 1. Calcolo Logaritmico + Smoothing
            for (let i = 0; i < 2; i++) {
                const db = 20 * Math.log10(Math.max(levels[i], 0.001));
                let targetVisual = (db - MIN_DB) / RANGE_DB;
                targetVisual = Math.max(0, Math.min(1, targetVisual));
                const speed = targetVisual > smoothedLevels.current[i] ? ATTACK : RELEASE;
                smoothedLevels.current[i] += (targetVisual - smoothedLevels.current[i]) * speed;
            }

            ctx.fillStyle = '#0f172a';
            ctx.fillRect(0, 0, w, h);

            const barW = w * 0.35;
            const gap = w * 0.1;
            const margin = (w - (barW * 2 + gap)) / 2;

            const safeGradient = ctx.createLinearGradient(0, h, 0, h * (1 - ZERO_DB_POS));
            safeGradient.addColorStop(0, '#10b981');     // verde in basso
            safeGradient.addColorStop(0.66, '#f59e0b');  // amber
            safeGradient.addColorStop(1, '#ef4444');     // rosso giusto sotto 0 dB

            for (let i = 0; i < 2; i++) {
                const x = margin + i * (barW + gap);
                const level = smoothedLevels.current[i];

                // Sfondo incavato
                ctx.fillStyle = '#1e293b';
                ctx.fillRect(x, 0, barW, h);

                const safeH = ZERO_DB_POS * h;

                if (level <= ZERO_DB_POS) {
                    // Tutto sotto 0 dB
                    const levelH = level * h;
                    ctx.fillStyle = safeGradient;
                    ctx.fillRect(x, h - levelH, barW, levelH);
                } else {
                    // Parte sicura: piena fino a 0 dB
                    ctx.fillStyle = safeGradient;
                    ctx.fillRect(x, h - safeH, barW, safeH);

                    // Parte overshoot: rosso brillante e saturo
                    const overshootH = (level - ZERO_DB_POS) * h;
                    ctx.fillStyle = '#fca5a5';
                    ctx.fillRect(x, h - (safeH + overshootH), barW, overshootH);
                }
            }

            ctx.strokeStyle = '#fbbf24';
            ctx.lineWidth = 1;
            ctx.beginPath();
            const zeroY = h - ZERO_DB_POS * h;
            ctx.moveTo(margin, zeroY);
            ctx.lineTo(w - margin, zeroY);
            ctx.stroke();
        };

        raf = requestAnimationFrame(render);
        return () => cancelAnimationFrame(raf);
    }, [levels]);

    return (
        <canvas ref={canvasRef} className="w-full h-full block" style={{ imageRendering: 'pixelated' }} />
    );
};