import { useState, useEffect, useRef, useCallback } from 'react';
import axios from 'axios';
import { Upload, X, Image as ImageIcon, Check, AlertCircle, RefreshCw } from 'lucide-react';

type Props = {
    /** URL endpoint upload (default: stessa logica del componente video) */
    uploadUrl?: string;
    /** Testo del bottone trigger */
    buttonLabel?: string;
    /** Callback dopo upload riuscito */
    onUploadComplete?: (response: any) => void;
    /** Estensioni accettate, default solo immagini */
    accept?: string;
    /** Dimensione massima file in MB (warning UI, validazione effettiva lato server) */
    maxSizeMB?: number;
};

type Phase = 'idle' | 'uploading' | 'success' | 'error';

const formatBytes = (bytes: number): string => {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
};

const ImageUploaderDialog = ({
                                 uploadUrl = 'https://192.168.188.254:8848/upload',
                                 buttonLabel = 'Upload Image',
                                 onUploadComplete,
                                 accept = 'image/*',
                                 maxSizeMB = 50,
                             }: Props) => {
    const [open, setOpen] = useState(false);
    const [file, setFile] = useState<File | null>(null);
    const [previewUrl, setPreviewUrl] = useState<string | null>(null);
    const [imgDims, setImgDims] = useState<{ w: number; h: number } | null>(null);
    const [progress, setProgress] = useState(0);
    const [phase, setPhase] = useState<Phase>('idle');
    const [errorMsg, setErrorMsg] = useState<string>('');
    const [dragActive, setDragActive] = useState(false);

    const inputRef = useRef<HTMLInputElement | null>(null);

    // Cleanup object URL quando il file cambia o il componente si smonta
    useEffect(() => {
        if (!previewUrl) return;
        return () => {
            URL.revokeObjectURL(previewUrl);
        };
    }, [previewUrl]);

    const resetAll = useCallback(() => {
        setFile(null);
        setPreviewUrl(null);
        setImgDims(null);
        setProgress(0);
        setPhase('idle');
        setErrorMsg('');
    }, []);

    const closeDialog = useCallback(() => {
        // Non permettere di chiudere durante l'upload
        if (phase === 'uploading') return;
        setOpen(false);
        // reset rimandato di un tick così non vede il flash mentre si chiude
        setTimeout(resetAll, 200);
    }, [phase, resetAll]);

    // Esc per chiudere
    useEffect(() => {
        if (!open) return;
        const onKey = (e: KeyboardEvent) => {
            if (e.key === 'Escape') closeDialog();
        };
        document.addEventListener('keydown', onKey);
        return () => document.removeEventListener('keydown', onKey);
    }, [open, closeDialog]);

    const handleFileSelected = (f: File | null) => {
        if (!f) return;

        // validazione minima: deve essere un'immagine
        if (!f.type.startsWith('image/')) {
            setErrorMsg('Il file selezionato non è un\'immagine.');
            setPhase('error');
            return;
        }

        // warning size
        if (f.size > maxSizeMB * 1024 * 1024) {
            setErrorMsg(`Il file è più grande di ${maxSizeMB} MB.`);
            setPhase('error');
            return;
        }

        // pulisci eventuale preview precedente
        if (previewUrl) URL.revokeObjectURL(previewUrl);

        const url = URL.createObjectURL(f);
        setFile(f);
        setPreviewUrl(url);
        setImgDims(null);
        setProgress(0);
        setPhase('idle');
        setErrorMsg('');

        // misura dimensioni quando l'immagine carica
        const img = new Image();
        img.onload = () => setImgDims({ w: img.naturalWidth, h: img.naturalHeight });
        img.src = url;
    };

    const handleInputChange = (e: React.ChangeEvent<HTMLInputElement>) => {
        const f = e.target.files?.[0] ?? null;
        handleFileSelected(f);
        // resetta il valore così la stessa selezione successiva triggera onChange
        e.target.value = '';
    };

    const handleDrop = (e: React.DragEvent<HTMLDivElement>) => {
        e.preventDefault();
        e.stopPropagation();
        setDragActive(false);
        const f = e.dataTransfer.files?.[0] ?? null;
        handleFileSelected(f);
    };

    const handleDragOver = (e: React.DragEvent<HTMLDivElement>) => {
        e.preventDefault();
        e.stopPropagation();
        if (!dragActive) setDragActive(true);
    };

    const handleDragLeave = (e: React.DragEvent<HTMLDivElement>) => {
        e.preventDefault();
        e.stopPropagation();
        setDragActive(false);
    };

    const handleUpload = async () => {
        if (!file) return;
        const formData = new FormData();
        formData.append('file', file);

        try {
            setPhase('uploading');
            setProgress(0);
            setErrorMsg('');

            const response = await axios.post(uploadUrl, formData, {
                onUploadProgress: (progressEvent) => {
                    const total = progressEvent.total ?? file.size;
                    const pct = Math.round((progressEvent.loaded * 100) / total);
                    setProgress(pct);
                },
            });

            if (response.status >= 200 && response.status < 300) {
                setPhase('success');
                onUploadComplete?.(response.data);
            } else {
                setPhase('error');
                setErrorMsg(`Status ${response.status}`);
            }
        } catch (error: any) {
            setPhase('error');
            setErrorMsg(error?.message ?? 'Errore sconosciuto durante l\'upload');
        }
    };

    return (
        <>
            {/* Trigger button */}
            <button
                onClick={() => setOpen(true)}
                className="flex items-center gap-2 px-4 py-2 bg-emerald-500/20 hover:bg-emerald-500/30 border border-emerald-500 text-emerald-400 rounded-md text-xs font-bold uppercase tracking-wide transition-colors"
            >
                <Upload size={14} />
                {buttonLabel}
            </button>

            {/* Dialog */}
            {open && (
                <div
                    className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 backdrop-blur-sm"
                    onClick={closeDialog}
                >
                    <div
                        className="relative w-full max-w-2xl mx-4 bg-zinc-900 border border-slate-700 rounded-xl shadow-2xl"
                        onClick={(e) => e.stopPropagation()}
                    >
                        {/* Header */}
                        <div className="flex items-center justify-between px-5 py-3 border-b border-slate-700">
                            <h3 className="text-emerald-400 font-bold text-sm flex items-center gap-2">
                                <ImageIcon size={16} />
                                Upload Image
                            </h3>
                            <button
                                onClick={closeDialog}
                                disabled={phase === 'uploading'}
                                className="p-1 rounded hover:bg-slate-700 text-slate-400 disabled:opacity-30 disabled:cursor-not-allowed"
                                title={phase === 'uploading' ? 'Upload in corso...' : 'Chiudi'}
                            >
                                <X size={18} />
                            </button>
                        </div>

                        {/* Body */}
                        <div className="p-5">
                            {/* === STATO: NESSUN FILE === */}
                            {!file && (
                                <div
                                    onClick={() => inputRef.current?.click()}
                                    onDrop={handleDrop}
                                    onDragOver={handleDragOver}
                                    onDragLeave={handleDragLeave}
                                    className={`border-2 border-dashed rounded-lg p-12 flex flex-col items-center justify-center cursor-pointer transition-colors ${
                                        dragActive
                                            ? 'border-emerald-500 bg-emerald-500/10'
                                            : 'border-slate-600 hover:border-slate-500 bg-slate-800/30'
                                    }`}
                                >
                                    <Upload size={40} className={dragActive ? 'text-emerald-400' : 'text-slate-500'} />
                                    <p className="mt-3 text-sm text-slate-300 font-bold">
                                        {dragActive ? 'Drop here' : 'Drag An Image'}
                                    </p>
                                    <p className="text-xs text-slate-500 mt-1">
                                        click to browse
                                    </p>
                                    <p className="text-[10px] text-slate-600 mt-3">
                                        max {maxSizeMB} MB
                                    </p>
                                    <input
                                        ref={inputRef}
                                        type="file"
                                        accept={accept}
                                        onChange={handleInputChange}
                                        className="hidden"
                                    />
                                </div>
                            )}

                            {/* === STATO: FILE SELEZIONATO === */}
                            {file && previewUrl && (
                                <div className="flex flex-col gap-4">
                                    {/* Preview immagine */}
                                    <div className="relative bg-black rounded-lg overflow-hidden border border-slate-700"
                                         style={{
                                             backgroundImage: 'linear-gradient(45deg, #1e293b 25%, transparent 25%), linear-gradient(-45deg, #1e293b 25%, transparent 25%), linear-gradient(45deg, transparent 75%, #1e293b 75%), linear-gradient(-45deg, transparent 75%, #1e293b 75%)',
                                             backgroundSize: '20px 20px',
                                             backgroundPosition: '0 0, 0 10px, 10px -10px, -10px 0px',
                                         }}
                                    >
                                        <img
                                            src={previewUrl}
                                            alt="preview"
                                            className="w-full max-h-[400px] object-contain"
                                        />
                                    </div>

                                    {/* Info file */}
                                    <div className="flex items-center gap-3 text-xs">
                                        <div className="flex-1 min-w-0">
                                            <p className="text-slate-200 font-medium truncate">{file.name}</p>
                                            <p className="text-slate-500 text-[10px] mt-0.5 font-mono">
                                                {formatBytes(file.size)}
                                                {imgDims && ` • ${imgDims.w}×${imgDims.h}px`}
                                                {file.type && ` • ${file.type}`}
                                            </p>
                                        </div>
                                        {phase === 'idle' && (
                                            <button
                                                onClick={() => inputRef.current?.click()}
                                                className="flex items-center gap-1 px-3 py-1.5 bg-slate-700 hover:bg-slate-600 rounded text-[10px] font-bold uppercase text-slate-300"
                                            >
                                                <RefreshCw size={11} />
                                                Cambia
                                            </button>
                                        )}
                                        <input
                                            ref={inputRef}
                                            type="file"
                                            accept={accept}
                                            onChange={handleInputChange}
                                            className="hidden"
                                        />
                                    </div>

                                    {/* Progress bar */}
                                    {phase === 'uploading' && (
                                        <div className="flex flex-col gap-1.5">
                                            <div className="flex items-center justify-between text-[10px] font-mono text-slate-400">
                                                <span>Caricamento...</span>
                                                <span>{progress}%</span>
                                            </div>
                                            <div className="w-full h-2 bg-slate-700 rounded-full overflow-hidden">
                                                <div
                                                    className="h-full bg-emerald-500 transition-all duration-200"
                                                    style={{ width: `${progress}%` }}
                                                />
                                            </div>
                                        </div>
                                    )}

                                    {/* Success */}
                                    {phase === 'success' && (
                                        <div className="flex items-center gap-2 px-4 py-3 bg-emerald-500/10 border border-emerald-500/30 rounded-lg text-emerald-400 text-sm">
                                            <Check size={16} />
                                            <span>Upload completato con successo</span>
                                        </div>
                                    )}

                                    {/* Error */}
                                    {phase === 'error' && (
                                        <div className="flex items-start gap-2 px-4 py-3 bg-rose-500/10 border border-rose-500/30 rounded-lg text-rose-300 text-sm">
                                            <AlertCircle size={16} className="flex-shrink-0 mt-0.5" />
                                            <div className="flex-1">
                                                <p className="font-medium">Errore</p>
                                                <p className="text-[11px] text-rose-400/80 mt-0.5">{errorMsg}</p>
                                            </div>
                                        </div>
                                    )}
                                </div>
                            )}
                        </div>

                        {/* Footer */}
                        <div className="flex items-center justify-end gap-2 px-5 py-3 border-t border-slate-700 bg-slate-900/50">
                            {phase === 'idle' && (
                                <>
                                    <button
                                        onClick={closeDialog}
                                        className="px-4 py-2 text-xs font-bold uppercase text-slate-400 hover:text-slate-200"
                                    >
                                        Cancel
                                    </button>
                                    <button
                                        onClick={handleUpload}
                                        disabled={!file}
                                        className="flex items-center gap-2 px-4 py-2 bg-emerald-500 hover:bg-emerald-400 disabled:bg-slate-700 disabled:text-slate-500 disabled:cursor-not-allowed text-black rounded text-xs font-bold uppercase transition-colors"
                                    >
                                        <Upload size={12} />
                                        Load
                                    </button>
                                </>
                            )}

                            {phase === 'uploading' && (
                                <button
                                    disabled
                                    className="px-4 py-2 bg-slate-700 text-slate-400 rounded text-xs font-bold uppercase cursor-not-allowed"
                                >
                                    Caricamento in corso...
                                </button>
                            )}

                            {phase === 'success' && (
                                <>
                                    <button
                                        onClick={resetAll}
                                        className="px-4 py-2 text-xs font-bold uppercase text-slate-400 hover:text-slate-200"
                                    >
                                        Carica un'altra
                                    </button>
                                    <button
                                        onClick={closeDialog}
                                        className="px-4 py-2 bg-emerald-500 hover:bg-emerald-400 text-black rounded text-xs font-bold uppercase"
                                    >
                                        Chiudi
                                    </button>
                                </>
                            )}

                            {phase === 'error' && (
                                <>
                                    <button
                                        onClick={closeDialog}
                                        className="px-4 py-2 text-xs font-bold uppercase text-slate-400 hover:text-slate-200"
                                    >
                                        Annulla
                                    </button>
                                    <button
                                        onClick={handleUpload}
                                        className="flex items-center gap-2 px-4 py-2 bg-amber-500 hover:bg-amber-400 text-black rounded text-xs font-bold uppercase"
                                    >
                                        <RefreshCw size={12} />
                                        Riprova
                                    </button>
                                </>
                            )}
                        </div>
                    </div>
                </div>
            )}
        </>
    );
};

export default ImageUploaderDialog;
