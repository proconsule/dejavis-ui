class PCMProcessor extends AudioWorkletProcessor {
    constructor() {
        super();

        this.CAPACITY = 131072;
        this.ring     = new Float32Array(this.CAPACITY);
        this.writePos = 0;
        this.readPos  = 0;
        this.size     = 0;

        // Parametri adaptive buffering
        this.TARGET_LATENCY   = 4096;
        this.LOW_WATER        = 1024;
        this.HIGH_WATER       = 65536;
        this.playing          = false;

        this.playbackRate     = 1.0;
        this.fractionalPos    = 0.0;

        // --- FFT ---
        // Raccoglie i campioni L+R mixati in un ring buffer di dimensione FFT_SIZE
        // e calcola le bande ogni FFT_INTERVAL campioni di output.
        this.FFT_SIZE         = 256;       // deve essere potenza di 2
        this.NUM_BARS         = 16;
        this.FFT_INTERVAL     = 2048;      // ~43ms a 48kHz → ~23 fps
        this.fftBuf           = new Float32Array(this.FFT_SIZE);
        this.fftWritePos      = 0;
        this.fftSampleCount   = 0;

        // Window di Hann precalcolata per ridurre spectral leakage
        this.hannWindow = new Float32Array(this.FFT_SIZE);
        for (let n = 0; n < this.FFT_SIZE; n++) {
            this.hannWindow[n] = 0.5 * (1 - Math.cos((2 * Math.PI * n) / (this.FFT_SIZE - 1)));
        }

        // Mapping logaritmico bande → bin FFT (precalcolato)
        // Usa sampleRate globale dell'AudioWorklet (disponibile come costante)
        this._barBins = this._buildBarBins();
        this.fftSmoothed = new Float32Array(this.NUM_BARS);
        this.FFT_SMOOTHING = 0.75; // 0=nessuno smoothing, 0.95=molto lento

        this.port.onmessage = ({ data }) => {
            if (!data) return;

            const incoming = data.length;

            if (this.size + incoming > this.CAPACITY) {
                const toDrop = this.size + incoming - this.CAPACITY;
                this._advance(toDrop);
            }

            for (let i = 0; i < incoming; i++) {
                this.ring[this.writePos] = data[i];
                this.writePos = (this.writePos + 1) % this.CAPACITY;
            }
            this.size += incoming;

            if (!this.playing && this.size >= this.TARGET_LATENCY) {
                this.playing = true;
            }
        };
    }

    // Precalcola [startBin, endBin, label] per ciascuna delle NUM_BARS bande
    // su scala logaritmica tra ~20Hz e Nyquist.
    _buildBarBins() {
        const nyquist    = sampleRate / 2;
        const freqMin    = 20;
        const freqMax    = nyquist;
        const halfFFT    = this.FFT_SIZE / 2;
        const bars       = [];

        for (let i = 0; i < this.NUM_BARS; i++) {
            const fLow  = freqMin * Math.pow(freqMax / freqMin,  i      / this.NUM_BARS);
            const fHigh = freqMin * Math.pow(freqMax / freqMin, (i + 1) / this.NUM_BARS);
            const fMid  = freqMin * Math.pow(freqMax / freqMin, (i + 0.5) / this.NUM_BARS);

            const binLow  = Math.max(0,         Math.round(fLow  * this.FFT_SIZE / sampleRate));
            const binHigh = Math.min(halfFFT - 1, Math.round(fHigh * this.FFT_SIZE / sampleRate));
            const safeHigh = Math.max(binLow + 1, binHigh);

            const label = fMid >= 1000 ? `${(fMid / 1000).toFixed(1)}k` : `${Math.round(fMid)}`;

            bars.push({ low: binLow, high: safeHigh, label });
        }
        return bars;
    }

    // DFT reale con finestra di Hann.
    // Opera sul ring buffer fftBuf (già scritto in ordine circolare).
    // Sostituisci _computeFFT() con questa versione
    _computeFFT() {
        const N     = this.FFT_SIZE;
        const halfN = N / 2;

        // Copia dal ring buffer con finestra di Hann in array separati re/im
        const re = new Float32Array(N);
        const im = new Float32Array(N);
        for (let n = 0; n < N; n++) {
            re[n] = this.fftBuf[(this.fftWritePos + n) % N] * this.hannWindow[n];
        }

        // FFT Cooley-Tukey iterativa (bit-reversal + butterfly)
        // Bit-reversal permutation
        let j = 0;
        for (let i = 1; i < N; i++) {
            let bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                [re[i], re[j]] = [re[j], re[i]];
                [im[i], im[j]] = [im[j], im[i]];
            }
        }

        // Butterfly
        for (let len = 2; len <= N; len <<= 1) {
            const ang = (2 * Math.PI) / len;
            const wRe = Math.cos(ang);
            const wIm = -Math.sin(ang);
            for (let i = 0; i < N; i += len) {
                let curRe = 1, curIm = 0;
                for (let k = 0; k < len / 2; k++) {
                    const uRe = re[i + k];
                    const uIm = im[i + k];
                    const vRe = re[i + k + len/2] * curRe - im[i + k + len/2] * curIm;
                    const vIm = re[i + k + len/2] * curIm + im[i + k + len/2] * curRe;
                    re[i + k]         = uRe + vRe;
                    im[i + k]         = uIm + vIm;
                    re[i + k + len/2] = uRe - vRe;
                    im[i + k + len/2] = uIm - vIm;
                    const nextRe = curRe * wRe - curIm * wIm;
                    curIm        = curRe * wIm + curIm * wRe;
                    curRe        = nextRe;
                }
            }
        }

        // Magnitudini e aggregazione bande (identico a prima)
        const bars = new Array(this.NUM_BARS);
        for (let i = 0; i < this.NUM_BARS; i++) {
            const { low, high, label } = this._barBins[i];
            let sum = 0;
            for (let k = low; k < high; k++) {
                sum += Math.sqrt(re[k] * re[k] + im[k] * im[k]) / N;
            }
            const avg = sum / (high - low);

            const dB         = avg > 0 ? 20 * Math.log10(avg) : -100;
            const minDb      = -100, maxDb = 0;
            const normalized = Math.max(0, (dB - minDb) / (maxDb - minDb));
            const sensitivity = 1.0 + i * 0.02;
            const raw        = Math.min(Math.pow(normalized * sensitivity, 0.9), 1.0);

            const prev     = this.fftSmoothed[i];
            const smoothed = raw > prev
                ? raw  * (1 - this.FFT_SMOOTHING * 0.3) + prev * (this.FFT_SMOOTHING * 0.3)
                : raw  * (1 - this.FFT_SMOOTHING)        + prev * this.FFT_SMOOTHING;

            this.fftSmoothed[i] = smoothed;
            bars[i] = { value: smoothed, label };
        }
        return bars;
    }

    _advance(n) {
        this.readPos = (this.readPos + n) % this.CAPACITY;
        this.size   -= n;
    }

    _readSample() {
        if (this.size === 0) return 0;
        const s = this.ring[this.readPos];
        this.readPos = (this.readPos + 1) % this.CAPACITY;
        this.size--;
        return s;
    }

    _peekAt(offset) {
        if (offset >= this.size) return 0;
        return this.ring[(this.readPos + offset) % this.CAPACITY];
    }

    process(inputs, outputs) {
        const output = outputs[0];
        if (!output || output.length === 0) return true;

        const channelL = output[0];
        const channelR = output[1] || output[0];
        const frames   = channelL.length; // tipicamente 128

        if (this.size < this.LOW_WATER) {
            this.playbackRate = 0.97;
        } else if (this.size > this.HIGH_WATER) {
            this.playbackRate = 1.03;
        } else {
            this.playbackRate += (1.0 - this.playbackRate) * 0.01;
        }

        if (!this.playing) {
            channelL.fill(0);
            channelR.fill(0);
            this.port.postMessage({ type: 'levels', l: 0, r: 0 });
            return true;
        }

        let maxL = 0, maxR = 0;

        for (let i = 0; i < frames; i++) {
            const intPos = Math.floor(this.fractionalPos);
            const frac   = this.fractionalPos - intPos;

            const s0L = this._peekAt(intPos * 2);
            const s0R = this._peekAt(intPos * 2 + 1);
            const s1L = this._peekAt(intPos * 2 + 2);
            const s1R = this._peekAt(intPos * 2 + 3);

            channelL[i] = s0L + frac * (s1L - s0L);
            channelR[i] = s0R + frac * (s1R - s0R);

            if (Math.abs(channelL[i]) > maxL) maxL = Math.abs(channelL[i]);
            if (Math.abs(channelR[i]) > maxR) maxR = Math.abs(channelR[i]);

            // Alimenta il ring FFT con il mix mono dei campioni interpolati
            this.fftBuf[this.fftWritePos] = (channelL[i] + channelR[i]) * 0.5;
            this.fftWritePos = (this.fftWritePos + 1) % this.FFT_SIZE;

            this.fractionalPos += this.playbackRate;
        }

        const samplesConsumed = Math.floor(this.fractionalPos) * 2;
        const toConsume       = Math.min(samplesConsumed, this.size);
        this._advance(toConsume);
        this.fractionalPos -= Math.floor(this.fractionalPos);

        if (this.size === 0) {
            this.playing = false;
        }

        // Invia levels ogni process() call (~3ms)
        this.port.postMessage({
            type:     'levels',
            l:        maxL,
            r:        maxR,
            bufferMs: Math.round((this.size / 2 / sampleRate) * 1000),
            underrun: !this.playing
        });

        // Invia FFT ogni FFT_INTERVAL campioni (~43ms, ~23fps)
        this.fftSampleCount += frames;
        if (this.fftSampleCount >= this.FFT_INTERVAL) {
            this.fftSampleCount = 0;
            this.port.postMessage({
                type: 'fft',
                data: this._computeFFT()
            });
        }

        return true;
    }
}

registerProcessor('pcm-processor', PCMProcessor);