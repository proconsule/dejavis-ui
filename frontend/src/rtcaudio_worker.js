class RTCAudioProcessor extends AudioWorkletProcessor {
    constructor() {
        super();

        // Configurazione FFT (Dalla tua logica originale)
        this.FFT_SIZE = 256;
        this.NUM_BARS = 16;
        this.FFT_INTERVAL = 2048; // ~43ms a 48kHz
        this.fftBuf = new Float32Array(this.FFT_SIZE);
        this.fftWritePos = 0;
        this.fftSampleCount = 0;

        // Window di Hann per ridurre lo spectral leakage
        this.hannWindow = new Float32Array(this.FFT_SIZE);
        for (let n = 0; n < this.FFT_SIZE; n++) {
            this.hannWindow[n] = 0.5 * (1 - Math.cos((2 * Math.PI * n) / (this.FFT_SIZE - 1)));
        }

        // Smoothing e mapping logaritmico
        this.FFT_SMOOTHING = 0.75;
        this.fftSmoothed = new Float32Array(this.NUM_BARS);
        this._barBins = this._buildBarBins();
    }

    _buildBarBins() {
        const nyquist = sampleRate / 2;
        const freqMin = 20;
        const freqMax = nyquist;
        const halfFFT = this.FFT_SIZE / 2;
        const bars = [];

        for (let i = 0; i < this.NUM_BARS; i++) {
            const fLow = freqMin * Math.pow(freqMax / freqMin, i / this.NUM_BARS);
            const fHigh = freqMin * Math.pow(freqMax / freqMin, (i + 1) / this.NUM_BARS);
            const fMid = freqMin * Math.pow(freqMax / freqMin, (i + 0.5) / this.NUM_BARS);

            const binLow = Math.max(0, Math.round(fLow * this.FFT_SIZE / sampleRate));
            const binHigh = Math.min(halfFFT - 1, Math.round(fHigh * this.FFT_SIZE / sampleRate));
            const safeHigh = Math.max(binLow + 1, binHigh);
            const label = fMid >= 1000 ? `${(fMid / 1000).toFixed(1)}k` : `${Math.round(fMid)}`;

            bars.push({ low: binLow, high: safeHigh, label });
        }
        return bars;
    }

    _computeFFT() {
        const N = this.FFT_SIZE;
        const re = new Float32Array(N);
        const im = new Float32Array(N);

        // Copia con finestra di Hann
        for (let n = 0; n < N; n++) {
            re[n] = this.fftBuf[(this.fftWritePos + n) % N] * this.hannWindow[n];
        }

        // FFT Cooley-Tukey iterativa (Logica dal tuo pcm_worker.js)
        let j = 0;
        for (let i = 1; i < N; i++) {
            let bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) [re[i], re[j]] = [re[j], re[i]];
        }

        for (let len = 2; len <= N; len <<= 1) {
            const ang = (2 * Math.PI) / len;
            const wRe = Math.cos(ang);
            const wIm = -Math.sin(ang);
            for (let i = 0; i < N; i += len) {
                let curRe = 1, curIm = 0;
                for (let k = 0; k < len / 2; k++) {
                    const uRe = re[i + k], uIm = im[i + k];
                    const vRe = re[i + k + len/2] * curRe - im[i + k + len/2] * curIm;
                    const vIm = re[i + k + len/2] * curIm + im[i + k + len/2] * curRe;
                    re[i + k] = uRe + vRe;
                    im[i + k] = uIm + vIm;
                    re[i + k + len/2] = uRe - vRe;
                    im[i + k + len/2] = uIm - vIm;
                    const nextRe = curRe * wRe - curIm * wIm;
                    curIm = curRe * wIm + curIm * wRe;
                    curRe = nextRe;
                }
            }
        }

        const outputData = new Float32Array(this.NUM_BARS);

        for (let i = 0; i < this.NUM_BARS; i++) {
            const bin = this._barBins[i];
            let sum = 0;
            for (let k = bin.low; k < bin.high; k++) {
                sum += Math.sqrt(re[k] * re[k] + im[k] * im[k]) / N;
            }
            const avg = sum / (bin.high - bin.low);
            const dB = avg > 0 ? 20 * Math.log10(avg) : -100;
            const normalized = Math.max(0, (dB + 100) / 100);
            const freqBoost = 1.0 + (i * 0.02); // Boost leggerissimo per le alte
            const raw = Math.min(Math.pow(normalized * freqBoost, 0.9), 1.0);

            this.fftSmoothed[i] = raw > this.fftSmoothed[i]
                ? raw * 0.4 + this.fftSmoothed[i] * 0.6
                : raw * 0.2 + this.fftSmoothed[i] * 0.8;

            outputData[i] = this.fftSmoothed[i]; // Salviamo solo il valore numerico
        }
        return outputData;
        /*
        // Output formattato per la UI
        return this._barBins.map((bin, i) => {
            let sum = 0;
            for (let k = bin.low; k < bin.high; k++) {
                sum += Math.sqrt(re[k] * re[k] + im[k] * im[k]) / N;
            }
            const avg = sum / (bin.high - bin.low);
            const dB = avg > 0 ? 20 * Math.log10(avg) : -100;
            const normalized = Math.max(0, (dB + 90) / 70);
            const raw = Math.min(Math.pow(normalized * (0.7 + i * 0.15), 0.85), 1.0);

            // Smoothing
            this.fftSmoothed[i] = raw > this.fftSmoothed[i]
                ? raw * 0.4 + this.fftSmoothed[i] * 0.6
                : raw * 0.2 + this.fftSmoothed[i] * 0.8;

            return { value: this.fftSmoothed[i], label: bin.label };
        });

         */
    }

    process(inputs, outputs) {
        const input = inputs[0];
        if (!input || !input[0]) return true;






        const channelL = input[0];
        const channelR = input[1] || input[0];

        // Catturiamo i campioni per la FFT
        for (let i = 0; i < channelL.length; i++) {
            const mono = (channelL[i] + channelR[i]) * 0.5;
            this.fftBuf[this.fftWritePos] = mono;
            this.fftWritePos = (this.fftWritePos + 1) % this.FFT_SIZE;
        }

        this.fftSampleCount += channelL.length;
        /*
        if (this.fftSampleCount >= this.FFT_INTERVAL) {
            this.fftSampleCount = 0;
            this.port.postMessage({
                type: 'fft',
                data: this._computeFFT()
            });
        }
        */
        if (this.fftSampleCount >= this.FFT_INTERVAL) {
            this.fftSampleCount = 0;
            const fftData = this._computeFFT();
            this.port.postMessage({
                type: 'fft',
                data: fftData
            }, [fftData.buffer]);
        }

        // IMPORTANTE: Passiamo l'audio all'output per non silenziarlo
        const output = outputs[0];
        if (output[0]) output[0].set(channelL);
        if (output[1]) output[1].set(channelR);

        return true;
    }
}

registerProcessor('rtcaudio-processor', RTCAudioProcessor);