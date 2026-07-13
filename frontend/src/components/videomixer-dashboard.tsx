import { useEffect, useState, useRef } from "react";
import { useWS } from '../WebSocketContext';
import { Rnd } from "react-rnd";
import {
    Sparkles, Image as ImageIcon, Film, CircleSlash,
    Eye, EyeOff, Plus, Trash2, Play, Square, type LucideIcon, Maximize, RotateCcw
} from 'lucide-react';
import ImageUploaderDialog from "@/components/video/imageuploader.tsx";
import MixerPreviewBackground from "@/components/video/videomixerpreview_borderless.tsx";
import { useGlobalWebRTC } from "@/components/rtc/WebRTCProvider.tsx";
//import { Pipette } from 'lucide-react';
import { Decimal } from 'decimal.js';
import {ChromaKeyControl} from "@/components/video/chromacontrol.tsx";
import {LumaKeyControl} from "@/components/video/lumacontrol.tsx";
import {Marquee} from "@/components/ui/marquee.tsx";
import {BusSelector_External, BusSelector_Layer} from "@/components/video/videobus_utils.tsx";

// =================================================================
//  TYPES
// =================================================================
type ChromaKeyParams = {
    v0: number; v1: number; v2: number;
    threshold: number; softness: number; spill: number; enabled: number;
};

type LumaKeyParams = {
    lower: number; upper: number; invert: number;
    softness: number; enabled: number;
};

type ColorParams = {
    brightness: number; contrast: number; saturation: number;
    gamma: number; hueShift: number; blackLevel: number; whiteLevel: number; enabled: number;
};


type filedecoder_data = {
    audio_bitrate: number;
    audio_channels: number;
    audio_codecName: string;
    audio_sampleRate: number;
    duration: number;
    filename: string;
    fps: number;
    height: number;
    interlaced: boolean;
    isPlaying: boolean;
    isResampling: boolean;
    position: number;
    video_bitrate: number;
    video_codecName: string;
    width: number;
}

type NDI_INPUT ={
    running: boolean;
    source: string;
    video:{
        width : number;
        height : number;
        fps: number;
        frameCount: number;
    };
    audio:{
        sourceRate: number;
        sourceCh: number;
        targetRate: number;
        targetCh: number;
        frameCount: number;
    };
}

type MixerInput = {
    alpha: number; height: number; width: number;
    inUse: boolean; isVisible: boolean; layer: number;
    pos_x: number; pos_y: number; scale_x: number; scale_y: number;
    type: number; keepaspect: boolean; y_flip?: boolean;
    busoutIdx: number;
    chromakey: ChromaKeyParams;
    lumakey : LumaKeyParams;
    color: ColorParams;
    file_decoder: filedecoder_data;
    ndi: {
        sources: {
            name: string;
            address: string;
        }[];
        status: NDI_INPUT;
        //sources: NDI_INPUT[];
    };
};

type MixerData = {
    webrtc_bus:number;
    display_bus:number;
    spout2_bus:number;
    videomixer: { inputs: MixerInput[] };
    core_h: number; core_w: number;
    window_w: number; window_h: number;
};

const defaultChroma: ChromaKeyParams = { v0: 0.0, v1: 1.0, v2: 0.0, threshold: 0.0, softness: 0.1, spill: 0.5, enabled: 0.0 };
const defaultColor: ColorParams = { brightness: 0.0, contrast: 1.0, saturation: 1.0, gamma: 1.0, hueShift: 0.0, blackLevel: 0.0, whiteLevel: 1.0, enabled: 0.0 };
const defaultLuma: LumaKeyParams = { lower: 0.0,upper:0.0,invert:0.0,softness:0.0,enabled:0.0};
const defaultNDI_INPUT: NDI_INPUT = {running: false,source:"",video:{width:0,height:0,fps:0,frameCount:0},audio:{sourceRate: 0,sourceCh:0,targetRate:0,targetCh:0,frameCount:0}};
const defaultFileDecoder: filedecoder_data = {
    audio_bitrate: 0,
    audio_channels: 0,
    audio_codecName: "",
    audio_sampleRate: 0,
    duration: 0,
    filename: "",
    fps: 0,
    height: 0,
    interlaced: false,
    isPlaying: false,
    isResampling: false,
    position: 0,
    video_bitrate: 0,
    video_codecName: "",
    width: 0,
};
const makeDefaultInput = (): MixerInput => ({
    alpha: 1, height: 0, width: 0, inUse: false, isVisible: true, layer: 0,
    pos_x: 0, pos_y: 0, scale_x: 1, scale_y: 1, type: -1,
    busoutIdx:0,
    keepaspect: false, y_flip: false,
    chromakey: { ...defaultChroma },
    lumakey:{...defaultLuma },
    color: { ...defaultColor },
    file_decoder: {...defaultFileDecoder },
    ndi :{status: defaultNDI_INPUT,sources:[]  }
});

type InputTypeMeta = {
    label: string; icon: LucideIcon; color: string; borderColor: string; bgColor: string;
};

const TYPE_META: Record<number, InputTypeMeta> = {
    0: { label: 'ProjectM', icon: Sparkles,  color: 'bg-purple-500/20 text-purple-300', borderColor: 'border-purple-500', bgColor: 'bg-purple-500/10' },
    1: { label: 'Image',    icon: ImageIcon, color: 'bg-sky-500/20 text-sky-300',       borderColor: 'border-sky-500',    bgColor: 'bg-sky-500/10' },
    2: { label: 'Video',    icon: Film,      color: 'bg-rose-500/20 text-rose-300',     borderColor: 'border-rose-500',   bgColor: 'bg-rose-500/10' },
    5: { label: 'NDI',    icon: Film,      color: 'bg-rose-500/20 text-rose-300',     borderColor: 'border-rose-500',   bgColor: 'bg-rose-500/10' },
};

const UNKNOWN_TYPE: InputTypeMeta = {
    label: 'Empty', icon: CircleSlash,
    color: 'bg-slate-700/50 text-slate-500',
    borderColor: 'border-slate-500', bgColor: 'bg-slate-500/10',
};

const getTypeMeta = (type: number): InputTypeMeta => TYPE_META[type] ?? UNKNOWN_TYPE;

const PROTECTED_INDEX = 0;

function sanitizeParams<T>(obj: T): T {
    const result = { ...obj } as any;
    for (const key in result) {
        if (typeof result[key] === 'number') {
            result[key] = new Decimal(result[key]).toDecimalPlaces(4).toNumber();
        } else if (typeof result[key] === 'object' && result[key] !== null) {
            result[key] = sanitizeParams(result[key]); // Gestisce i sotto-oggetti come 'color'
        }
    }
    return result;
}

// =================================================================
//  COMPONENTS DI SUPPORTO
// =================================================================
const ControlSlider = ({ label, value, min, max, step, onChange }: any) => {
    // Fallback a 0 se value è undefined per evitare il crash di .toFixed()
    const safeValue = value ?? 0;

    return (
        <div className="flex flex-col gap-1.5">
            <div className="flex justify-between items-center px-0.5">
                <span className="text-[10px] font-bold text-slate-400 uppercase tracking-tight">{label}</span>
                <span className="text-[10px] font-mono text-emerald-400 bg-black/30 px-1.5 rounded">
                    {safeValue.toFixed(step < 0.1 ? 3 : 2)}
                </span>
            </div>
            <input
                type="range"
                min={min}
                max={max}
                step={step}
                value={safeValue}
                onChange={(e) => onChange(parseFloat(e.target.value))}
                className="w-full h-1.5 bg-slate-700 rounded-lg appearance-none cursor-pointer accent-emerald-500"
            />
        </div>
    );
};

// =================================================================
//  CONTEXT MENU
// =================================================================
type ContextMenuState = {
    visible: boolean;
    x: number;
    y: number;
    inputIndex: number;
};

type ContextMenuProps = {
    state: ContextMenuState;
    input: MixerInput | null;
    onClose: () => void;
    onAddVideo: (idx: number) => void;
    onRemove: (idx: number) => void;
    onSelectNDISource: (idx: number, sourceName: string) => void; // Callback per la selezione
    onNDIRemove: (idx: number) => void;
    sendMessage: (msg: string) => void;
};

function ContextMenu({ state, input, onClose, onAddVideo, onRemove, onSelectNDISource,onNDIRemove }: ContextMenuProps) {
    const ref = useRef<HTMLDivElement | null>(null);

    const { sendMessage } = useWS();


    useEffect(() => {
        if (!state.visible) return;
        const handleClick = (e: MouseEvent) => {
            if (ref.current && !ref.current.contains(e.target as Node)) onClose();
        };
        document.addEventListener('mousedown', handleClick);
        return () => document.removeEventListener('mousedown', handleClick);
    }, [state.visible, onClose]);

    if (!state.visible || !input) return null;

    return (
        <div
            ref={ref}
            className="fixed z-50 min-w-[220px] bg-zinc-900 border border-slate-700 rounded-lg shadow-2xl py-2 text-xs"
            style={{ left: state.x, top: state.y }}
        >
            <div className="px-3 py-1.5 text-[9px] font-black uppercase text-emerald-500 border-b border-slate-800 mb-1 tracking-tighter">
                Input Settings #{state.inputIndex}
            </div>

            {/* SEZIONE NDI SPECIFICA */}
            {input.type === 5 ? (
                <div className="flex flex-col">
                    <div className="px-3 py-1 text-[8px] text-slate-500 font-bold uppercase">Available NDI Sources</div>
                    <div className="max-h-[200px] overflow-y-auto custom-scrollbar my-1">
                        {input.ndi?.sources?.length > 0 ? (
                            input.ndi?.sources.map((src, i) => (
                                <button
                                    key={i}
                                    onClick={() => { onSelectNDISource(state.inputIndex, src.name); onClose(); }}
                                    className="w-full text-left px-3 py-2 hover:bg-emerald-500/10 hover:text-emerald-400 border-l-2 border-transparent hover:border-emerald-500 transition-all"
                                >
                                    <div className="font-bold truncate">{src.name}</div>
                                    <div className="text-[8px] text-slate-500 font-mono">{src.address}</div>
                                </button>
                            ))
                        ) : (
                            <div className="px-3 py-3 text-slate-600 italic text-center">No sources found...</div>
                        )}
                    </div>
                    <div className="border-t border-slate-800 mt-1 pt-1">
                        <button
                            onClick={() => { onNDIRemove(state.inputIndex); onClose(); }}
                            className="w-full text-left px-3 py-2 hover:bg-rose-500/10 flex items-center gap-2 text-rose-400"
                        >
                            <Trash2 size={12} /> Disconnect NDI
                        </button>
                    </div>
                </div>
            ) : (
                /* MENU STANDARD PER ALTRI TIPI */
                <>
                    {input.inUse ? (
                        <button
                            onClick={() => { onRemove(state.inputIndex); onClose(); }}
                            className="w-full text-left px-3 py-2 hover:bg-rose-500/10 flex items-center gap-2 text-rose-400"
                        >
                            <Trash2 size={12} /> Remove input
                        </button>
                    ) : (
                        <div className="flex flex-col gap-1">
                            <button
                                onClick={() => { onAddVideo(state.inputIndex); onClose(); }}
                                className="w-full text-left px-3 py-2 hover:bg-emerald-500/10 flex items-center gap-2 text-emerald-400"
                            >
                                <Plus size={12} /> Add Video Player
                            </button>
                            <div className="px-3 py-1">
                                <ImageUploaderDialog
                                    uploadUrl={`https://${window.location.hostname}:8848/upload`}
                                    buttonLabel="Load Image"
                                    onUploadComplete={() => onClose()}
                                />
                            </div>
                            <div className="px-3 py-1">
                                <button
                                    onClick={() => {
                                        sendMessage({ msgid: 20000, video_mixer_idx: state.inputIndex });
                                        onClose();
                                    }}
                                    className="w-full text-left px-2 py-2 hover:bg-emerald-500/10 flex items-center gap-2 text-emerald-400 transition-colors text-[10px] font-bold uppercase tracking-tight"
                                >
                                    <Play size={12} /> Trigger Action
                                </button>
                            </div>
                        </div>
                    )}
                </>
            )}
        </div>
    );
}

// =================================================================
//  SIDEBAR ITEM
// =================================================================
type SidebarItemProps = {
    idx: number;
    input: MixerInput;
    selected: boolean;
    onSelect: () => void;
    onContextMenu: (e: React.MouseEvent) => void;
    onToggleVisibility: () => void;
};

function SidebarItem({ idx, input, selected, onSelect, onContextMenu, onToggleVisibility }: SidebarItemProps) {
    const tm = getTypeMeta(input.type);
    const Icon = tm.icon;
    const Visibility = input.isVisible ? Eye : EyeOff;

    const isEmpty = !input.inUse && idx !== PROTECTED_INDEX;

    // Estrazione sicura dei dati NDI
    const isNDI = input.type === 5;
    const isAVPlayer = input.type === 2;
    const avplayerStatus = input?.file_decoder;

    const ndiStatus = input?.ndi?.status;

    // Label principale: Nome della sorgente
    const itemlabel = isNDI ? ndiStatus?.source || "Searching..." : "";

    return (
        <div
            onClick={onSelect}
            onContextMenu={onContextMenu}
            className={`group flex flex-col p-2 rounded border cursor-pointer transition-all ${
                selected
                    ? 'bg-emerald-500/20 border-emerald-500'
                    : isEmpty
                        ? 'bg-slate-900/50 border-transparent hover:bg-slate-800 opacity-50'
                        : 'bg-slate-800 border-transparent hover:bg-slate-700'
            }`}
        >
            <div className="flex justify-between items-center mb-1">
                <div className="flex items-center gap-2 min-w-0 w-full">
                    <button
                        onClick={(e) => { e.stopPropagation(); onToggleVisibility(); }}
                        className={`p-0.5 rounded hover:bg-slate-600 transition-colors ${
                            input.isVisible ? 'text-emerald-400' : 'text-slate-500'
                        }`}
                    >
                        <Visibility size={12} />
                    </button>

                    <span className={`text-[11px] font-bold ${selected ? 'text-emerald-400' : 'text-slate-400'}`}>
                        #{idx}
                    </span>

                    <span className={`text-[9px] px-1.5 py-0.5 rounded font-bold uppercase tracking-wide flex items-center gap-1 ${tm.color}`}>
                        <Icon size={10} />
                        {tm.label}
                    </span>

                    <div className="flex-1 min-w-0 text-[10px] font-medium text-slate-200">
                        <Marquee>{itemlabel}</Marquee>
                    </div>

                    {/* Indicatore di stato NDI (Running vs Stopped) */}
                    {isNDI && (
                        <span className={`text-[8px] ${ndiStatus?.running ? 'text-emerald-400' : 'text-red-500'} animate-pulse`}>
                            ●
                        </span>
                    )}
                </div>
            </div>

            {isAVPlayer && avplayerStatus && (
                <div>
                    <div className="flex gap-2  px-1 py-0.5 bg-black/30 rounded text-[8px] text-slate-300 font-mono">
                        <span className="text-blue-400">
                            {avplayerStatus.width}x{avplayerStatus.height}
                        </span>
                        <span className="text-purple-400">
                            {avplayerStatus.fps.toFixed(2)} FPS
                        </span>
                        <span className="text-purple-400">
                            {avplayerStatus.interlaced ? "Interlaced" : "Progressive" }
                        </span>

                    </div>
                    <div className="flex gap-2 mb-1.5 px-1 py-0.5 bg-black/30 rounded text-[8px] text-slate-300 font-mono">
                        <span className="text-blue-400">
                            {avplayerStatus.audio_sampleRate} Hz
                        </span>
                        <span className="text-purple-400">
                            {avplayerStatus.audio_channels} ch
                        </span>
                        <span className="text-purple-400">
                            {avplayerStatus.audio_bitrate} bps
                        </span>

                    </div>
                </div>

            )}
            {/* Area dettagli specifici NDI */}
            {isNDI && ndiStatus?.running && (
                <div className="flex gap-2 mb-1.5 px-1 py-0.5 bg-black/30 rounded text-[8px] text-slate-300 font-mono">
                    <span className="text-blue-400">
                        {ndiStatus.video.width}x{ndiStatus.video.height}
                    </span>
                    <span className="text-purple-400">
                        {ndiStatus.video.fps.toFixed(2)} FPS
                    </span>
                    <span className="text-amber-400">
                        {ndiStatus.audio.sourceRate / 1000}kHz {ndiStatus.audio.sourceCh}ch
                    </span>
                </div>
            )}

            {/* Footer standard */}
            <div className="flex justify-between items-center text-[9px] font-mono text-slate-500">
                <div className="flex gap-2">
                    <span>L:{input.layer}</span>
                    <span>α:{(input.alpha * 100).toFixed(0)}%</span>
                </div>
                <div className="flex gap-2 items-center">
                    <span className="text-green-500/80 font-bold text-[8px]">{input.busoutIdx === 0 ? "BUS A" : "BUS B"}</span>
                    {!input.isVisible && <span className="text-slate-600 italic">hidden</span>}
                </div>
            </div>
        </div>
    );
}

// =================================================================
//  MAIN DASHBOARD
// =================================================================
export function VideoMixerDashboard() {
    const { lastJsonMessage, sendMessage } = useWS();
    const { state, start, stop } = useGlobalWebRTC();

    const [webRtcBus, setWebRtcBus] = useState(0);
    const [spout2Bus, setspout2Bus] = useState(0);
    const [displayBus, setdisplayBus] = useState(0);

    const [mixerData, setMixerData] = useState<MixerData>({
        webrtc_bus:0,spout2_bus:0,display_bus:0,
        videomixer: { inputs: Array.from({ length: 10 }, makeDefaultInput) },
        core_h: 1080, core_w: 1920, window_w: 1920, window_h: 1080,
    });

    const [selectedIndex, setSelectedIndex] = useState(0);
    const [activeTab, setActiveTab] = useState<'bus'|'transform' | 'chroma' | 'luma' | 'color'>('transform');

    const [ctxMenu, setCtxMenu] = useState<ContextMenuState>({ visible: false, x: 0, y: 0, inputIndex: -1 });

    const PREVIEW_MAX_WIDTH = 800;
    const scaleFactor = Math.min(PREVIEW_MAX_WIDTH / mixerData.core_w, 1.0);
    const displayW = mixerData.core_w * scaleFactor;
    const displayH = mixerData.core_h * scaleFactor;

    useEffect(() => {
        if (!lastJsonMessage || lastJsonMessage.msgid !== 1) return;
        if (lastJsonMessage.video) {
            const incoming = lastJsonMessage.video as MixerData;
            const merged: MixerData = {
                ...incoming,
                videomixer: {
                    inputs: incoming.videomixer.inputs.map((inp) => ({
                        ...makeDefaultInput(),
                        ...inp,
                    })),
                },
            };
            const webrtc_bus = incoming.webrtc_bus;
            const display_bus = incoming.display_bus;
            const spout2_bus = incoming.spout2_bus;
            setspout2Bus(spout2_bus);
            setWebRtcBus(webrtc_bus);
            setdisplayBus(display_bus);
            setMixerData(merged);
        }
    }, [lastJsonMessage]);

    // Update Generico
    const updateInput = (index: number, newValues: Partial<MixerInput>) => {
        const newInputs = [...mixerData.videomixer.inputs];
        newInputs[index] = { ...newInputs[index], ...newValues };
        setMixerData(prev => ({ ...prev, videomixer: { inputs: newInputs } }));
        const cleanInput = sanitizeParams(newInputs[index]);
        sendMessage({ msgid: 8001, input_index: index, ...cleanInput });
    };

    // Update Chroma Key
    const updateChroma = (index: number, newChroma: Partial<ChromaKeyParams>) => {
        const current = mixerData.videomixer.inputs[index].chromakey || defaultChroma;
        const updated = { ...current, ...newChroma };

        const newInputs = [...mixerData.videomixer.inputs];
        newInputs[index] = { ...newInputs[index], chromakey: updated };
        setMixerData(prev => ({ ...prev, videomixer: { inputs: newInputs } }));

        sendMessage({ msgid: 8003, input_index: index, params: updated });
    };

    const updateLuma = (index: number, newLuma: Partial<LumaKeyParams>) => {
        const current = mixerData.videomixer.inputs[index].lumakey || defaultLuma;
        const updated = { ...current, ...newLuma };

        const newInputs = [...mixerData.videomixer.inputs];
        newInputs[index] = { ...newInputs[index], lumakey: updated };
        setMixerData(prev => ({ ...prev, videomixer: { inputs: newInputs } }));

        sendMessage({ msgid: 8004, input_index: index, params: updated });
    };

    // Update Color Params
    const updateColor = (index: number, newColor: Partial<ColorParams>) => {
        const current = mixerData.videomixer.inputs[index].color || defaultColor;
        const updated = { ...current, ...newColor };

        const newInputs = [...mixerData.videomixer.inputs];
        newInputs[index] = { ...newInputs[index], color: updated };
        setMixerData(prev => ({ ...prev, videomixer: { inputs: newInputs } }));

        sendMessage({ msgid: 8005, input_index: index, params: updated });
    };

    const toggleVisibility = (index: number) => {
        const next = !mixerData.videomixer.inputs[index].isVisible;
        sendMessage({msgid: 8002, input_index: index,isVisible:next});
        updateInput(index, { isVisible: next });
    };

    const changeLayer = (index: number, delta: number) => {
        updateInput(index, { layer: mixerData.videomixer.inputs[index].layer + delta });
    };

    const addVideoInput = (index: number) => sendMessage({ msgid: 8010, input_index: index });
    const removeInput = (index: number) => sendMessage({ msgid: 8011, input_index: index });

    const handleNDIRemove = (index: number) => sendMessage({ msgid: 9001, video_index: index });

    const handleWebRTCBusChange = (value: number) => {
        setWebRtcBus(value);
        sendMessage({ msgid: 10002, webrtc_videobusidx: value });
    };

    const handleSPOUT2Change = (value: number) => {
        setspout2Bus(value);
        sendMessage({ msgid: 10003, spout_videobusidx: value });
    };

    const handleDisplayBusChange = (value: number) => {
        setdisplayBus(value);
        sendMessage({ msgid: 10004, display_videobusidx: value });
    };

    const handleLayerBusChange = (idx:number,value: number) => {
        sendMessage({ msgid: 10001, videoidx: idx, busidx: value });
    };

    const toggleBicubic = (index: number, active: boolean) => {
        sendMessage({
            msgid: 8020,
            input_index: index,
            active: active
        });
    };

    const openContextMenu = (e: React.MouseEvent, index: number) => {
        if (index === PROTECTED_INDEX) return;
        e.preventDefault();
        setCtxMenu({ visible: true, x: e.clientX, y: e.clientY, inputIndex: index });
    };
    const closeContextMenu = () => setCtxMenu(prev => ({ ...prev, visible: false }));

    const pixelToNdcCenter = (px: number, display: number, ndcScale: number) =>
        (px / display) * 2 - 1 + ndcScale;
    const ndcCenterToPixelTopLeft = (ndcCenter: number, ndcScale: number, display: number) =>
        ((ndcCenter - ndcScale + 1) / 2) * display;

    const computeRenderedScale = (input: MixerInput): { sx: number; sy: number } => {
        if (!input.keepaspect || input.width <= 0 || input.height <= 0 ||
            mixerData.core_w <= 0 || mixerData.core_h <= 0) {
            return { sx: input.scale_x, sy: input.scale_y };
        }
        const textureRatio = input.width / input.height;
        const canvasRatio  = mixerData.core_w / mixerData.core_h;
        const baseScale    = input.scale_x;

        if (textureRatio > canvasRatio) {
            return { sx: baseScale, sy: baseScale * (canvasRatio / textureRatio) };
        } else {
            return { sx: baseScale * (textureRatio / canvasRatio), sy: baseScale };
        }
    };

    const selectedInput = mixerData.videomixer.inputs[selectedIndex];

    const ctxMenuInput = ctxMenu.visible && ctxMenu.inputIndex >= 0
        ? mixerData.videomixer.inputs[ctxMenu.inputIndex]
        : null;


    const isColorEnabled = selectedInput.color.enabled > 0;

    const previewRef = useRef<HTMLCanvasElement>(null);
    const [isPicking, setIsPicking] = useState(false);

    const handleCanvasClick = (e: React.MouseEvent) => {
        if (!isPicking || !previewRef.current) return;

        const canvas = previewRef.current;
        const ctx = canvas.getContext('2d', { willReadFrequently: true });
        if (!ctx) return;

        const rect = canvas.getBoundingClientRect();

        // Calcolo coordinate relative allo scaling del canvas
        const scaleX = canvas.width / rect.width;
        const scaleY = canvas.height / rect.height;

        const x = (e.clientX - rect.left) * scaleX;
        const y = (e.clientY - rect.top) * scaleY;

        // Preleviamo i dati del singolo pixel cliccato
        const pixel = ctx.getImageData(x, y, 1, 1).data;

        // Normalizzazione 0-255 -> 0.0-1.0 per il mixer Vulkan
        const r = pixel[0] / 255;
        const g = pixel[1] / 255;
        const b = pixel[2] / 255;

        console.log(`Colore campionato: R:${r} G:${g} B:${b}`);

        // Invio dei nuovi parametri al mixer
        updateChroma(selectedIndex, {
            v0: r,
            v1: g,
            v2: b
        });



        // Disattiva la modalità picker dopo il click
        setIsPicking(false);
    };


    const handleSelectNDISource = (index: number, sourceName: string) => {
        sendMessage({
            msgid: 9000,
            input_index: index,
            source_name: sourceName
        });
    };

    // Definizione stili dashboard (Risolve l'errore "tm is not defined")
    const dashTm = {
        bgColor: 'bg-zinc-900',
        color: 'text-white',
        borderColor: 'border-slate-700'
    };

    const setFullscreen = (idx: number) => {
        updateInput(idx, {
            pos_x: 0,
            pos_y: 0,
            scale_x: 1,
            scale_y: 1
        });
    };

    const resetColor = (idx: number) => {
        updateColor(idx, {
            brightness: 0.0,
            contrast: 1.0,
            saturation: 1.0,
            gamma: 1.0,
            hueShift: 0.0,
            blackLevel: 0.0,
            whiteLevel: 1.0
        });
    };

    console.log(mixerData)


    return (
        <div className={`p-6 ${dashTm.bgColor} ${dashTm.color} rounded-xl border ${dashTm.borderColor} flex flex-col gap-6 shadow-2xl font-sans`}>
            {/* Context menu */}
            <ContextMenu
                state={ctxMenu}
                input={ctxMenuInput}
                onClose={closeContextMenu}
                onAddVideo={addVideoInput}
                onRemove={removeInput}
                onSelectNDISource={handleSelectNDISource}
                onNDIRemove={handleNDIRemove}
                sendMessage={sendMessage}
            />

            {/* --- HEADER --- */}
            <div className="flex justify-between items-center">
                <div>
                    <h2 className="text-xl font-bold text-emerald-400">Vulkan Video Mixer</h2>
                    <p className="text-[10px] text-slate-500 font-mono uppercase tracking-widest">
                        Core: {mixerData.core_w}x{mixerData.core_h}
                    </p>
                </div>

                <div className="flex items-center gap-3">
                    <div className={`flex items-center gap-2 px-3 py-1.5 rounded-lg border transition-all ${
                        state === 'connected' ? 'bg-emerald-500/10 border-emerald-500/50 text-emerald-400' : 'bg-zinc-800 border-slate-700 text-slate-400'
                    }`}>
                        <div className={`w-2 h-2 rounded-full ${state === 'connected' ? 'bg-emerald-500 animate-pulse' : 'bg-slate-600'}`} />
                        <span className="text-[10px] font-bold uppercase tracking-wider">{state}</span>
                    </div>

                    {state !== 'connected' && state !== 'connecting' ? (
                        <button onClick={start} className="flex items-center gap-2 px-4 py-1.5 bg-emerald-600 hover:bg-emerald-500 text-white rounded shadow-lg transition-colors text-xs font-bold uppercase">
                            <Play size={14} fill="currentColor" /> Start Preview
                        </button>
                    ) : (
                        <button onClick={stop} className="flex items-center gap-2 px-4 py-1.5 bg-rose-600 hover:bg-rose-500 text-white rounded shadow-lg transition-colors text-xs font-bold uppercase">
                            <Square size={14} fill="currentColor" /> Stop Preview
                        </button>
                    )}
                </div>
            </div>

            <div className="flex gap-6">
                {/* --- SIDEBAR --- */}
                <div className="flex flex-col gap-2 w-64">
                    <div className="flex items-center justify-between px-2 mb-2">
                        <span className="text-[10px] font-bold text-slate-500 uppercase">Input Layers</span>
                    </div>
                    <div className="flex flex-col gap-1 overflow-y-auto max-h-[500px] pr-2 custom-scrollbar">
                        {mixerData.videomixer.inputs.map((input, idx) => (
                            <SidebarItem
                                key={idx} idx={idx} input={input}
                                selected={selectedIndex === idx}
                                onSelect={() => setSelectedIndex(idx)}
                                onContextMenu={(e) => openContextMenu(e, idx)}
                                onToggleVisibility={() => toggleVisibility(idx)}
                            />
                        ))}
                    </div>
                    <BusSelector_External label={"DISPLAY BUS"} value={displayBus} onChange={handleDisplayBusChange} />
                    <BusSelector_External label={"WebRTC BUS"} value={webRtcBus} onChange={handleWebRTCBusChange} />
                    <BusSelector_External label={"SPOUT2 BUS"} value={spout2Bus} onChange={handleSPOUT2Change} />
                </div>

                {/* --- PREVIEW AREA --- */}
                <div className="flex-1 flex flex-col items-center">
                    <div
                        className="relative bg-black border-2 border-slate-800 rounded shadow-2xl overflow-hidden"
                        style={{ width: displayW, height: displayH }}
                    >
                        <div className="absolute inset-0 z-0">
                            <MixerPreviewBackground ref={previewRef} allowControls={true} objectFit="contain" />
                        </div>

                        {/* Rendering Layers */}
                        {[...mixerData.videomixer.inputs]
                            .map((input, originalIndex) => ({ ...input, originalIndex }))
                            .filter(input => input.inUse)
                            .sort((a, b) => a.layer - b.layer)
                            .map((input) => {
                                const isSelected = input.originalIndex === selectedIndex;
                                const rendered = computeRenderedScale(input);
                                const itemTm = getTypeMeta(input.type);

                                if (isSelected && activeTab === 'transform') {
                                    const editScaleX = input.keepaspect ? rendered.sx : input.scale_x;
                                    const editScaleY = input.keepaspect ? rendered.sy : input.scale_y;
                                    const boxW = editScaleX * displayW;
                                    const boxH = editScaleY * displayH;

                                    return (
                                        <Rnd
                                            key={input.originalIndex}
                                            style={{ zIndex: input.layer + 100 }}
                                            size={{ width: boxW, height: boxH }}
                                            position={{
                                                x: ndcCenterToPixelTopLeft(input.pos_x, editScaleX, displayW),
                                                y: ndcCenterToPixelTopLeft(input.pos_y, editScaleY, displayH),
                                            }}
                                            disableDragging={isPicking}
                                            lockAspectRatio={input.keepaspect ? boxW / boxH : false}
                                            onDragStop={(_e, d) => {
                                                updateInput(selectedIndex, {
                                                    pos_x: pixelToNdcCenter(d.x, displayW, editScaleX),
                                                    pos_y: pixelToNdcCenter(d.y, displayH, editScaleY),
                                                });
                                            }}
                                            onResizeStop={(_e, _dir, ref, _delta, pos) => {
                                                const newSx = ref.offsetWidth / displayW;
                                                const newSy = ref.offsetHeight / displayH;
                                                const next: Partial<MixerInput> = {
                                                    pos_x: pixelToNdcCenter(pos.x, displayW, newSx),
                                                    pos_y: pixelToNdcCenter(pos.y, displayH, newSy),
                                                };
                                                if (input.keepaspect) {
                                                    const base = Math.max(newSx, newSy);
                                                    next.scale_x = base;
                                                    next.scale_y = base;
                                                } else {
                                                    next.scale_x = newSx;
                                                    next.scale_y = newSy;
                                                }
                                                updateInput(selectedIndex, next);
                                            }}
                                        >
                                            <div className={`w-full h-full border-2 ${itemTm.borderColor} relative`}>
                                                <div className="absolute top-0 left-0 px-1 text-[9px] font-bold bg-emerald-500 text-black">#{selectedIndex}</div>
                                            </div>
                                        </Rnd>
                                    );
                                }
                                return (
                                    <div key={input.originalIndex} className={`absolute border ${isSelected ? 'border-emerald-500 border-2' : 'border-slate-700'} pointer-events-none`}
                                         style={{
                                             zIndex: input.layer,
                                             width: rendered.sx * displayW,
                                             height: rendered.sy * displayH,
                                             left: ndcCenterToPixelTopLeft(input.pos_x, rendered.sx, displayW),
                                             top: ndcCenterToPixelTopLeft(input.pos_y, rendered.sy, displayH),
                                             opacity: input.isVisible ? input.alpha : 0.15,
                                         }}
                                    />
                                );
                            })
                        }

                        {/* Overlay Pipetta */}
                        {isPicking && (
                            <div className="absolute inset-0 z-[1000] cursor-crosshair bg-cyan-500/10 border-4 border-cyan-500" onClick={handleCanvasClick} />
                        )}
                    </div>

                    {/* --- CONTROLS TABS --- */}
                    <div className="mt-4 w-full bg-slate-800/50 rounded-lg border border-slate-700 overflow-hidden">
                        <div className="flex gap-1 border-b border-slate-700 p-1 bg-slate-800">
                            {(['bus','transform', 'chroma','luma', 'color'] as const).map(tab => (
                                <button key={tab} onClick={() => setActiveTab(tab)}
                                        className={`text-[10px] uppercase font-bold px-4 py-2 rounded flex-1 transition-colors ${activeTab === tab ? 'bg-slate-700 text-emerald-400' : 'text-slate-400 hover:bg-slate-700/50'}`}>
                                    {tab}
                                </button>
                            ))}
                        </div>

                        <div className="p-4 min-h-[240px]">
                            {activeTab === 'bus' && (
                                < BusSelector_Layer
                                    video_input_idx={selectedIndex}
                                    value={selectedInput.busoutIdx}
                                    onChange={handleLayerBusChange}
                                />
                            )}
                            {/* TAB TRANSFORM */}
                            {activeTab === 'transform' && (
                                <div className="flex flex-col gap-6">
                                    {/* Quick Actions Row */}
                                    <div className="flex gap-4 p-3 bg-black/20 rounded-lg border border-slate-700/50">
                                        <button
                                            onClick={() => setFullscreen(selectedIndex)}
                                            className="flex-1 flex items-center justify-center gap-2 py-2 bg-emerald-600/20 hover:bg-emerald-600/40 text-emerald-400 border border-emerald-500/30 rounded transition-all text-[10px] font-bold uppercase tracking-wider"
                                        >
                                            <Maximize size={14} /> Set Fullscreen
                                        </button>
                                        <div className="flex items-center gap-3 px-4 bg-slate-800 rounded border border-slate-700">
                                            <span className="text-[10px] font-bold text-slate-500 uppercase">Keep Aspect</span>
                                            <input
                                                type="checkbox"
                                                checked={selectedInput.keepaspect}
                                                onChange={(e) => updateInput(selectedIndex, { keepaspect: e.target.checked })}
                                                className="w-4 h-4 accent-emerald-500 cursor-pointer"
                                            />
                                        </div>
                                        <div className="flex items-center gap-3 px-4 bg-slate-800 rounded border border-slate-700">
                                            <span className="text-[10px] font-bold text-slate-500 uppercase">Bicubic</span>
                                            <label className="relative inline-flex items-center cursor-pointer">
                                                <input
                                                    type="checkbox"
                                                    className="sr-only peer"
                                                    onChange={(e) => toggleBicubic(selectedIndex, e.target.checked)}
                                                />
                                                <div className="w-8 h-4 bg-zinc-800 rounded-full peer peer-checked:bg-emerald-600 after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-slate-300 after:rounded-full after:h-3 after:w-3 after:transition-all peer-checked:after:translate-x-4 peer-checked:after:bg-white"></div>
                                            </label>
                                        </div>
                                    </div>

                                    {/* Sliders Grid */}
                                    <div className="grid grid-cols-2 gap-x-8 gap-y-4">
                                        <ControlSlider
                                            label="Alpha"
                                            value={selectedInput.alpha}
                                            min={0} max={1} step={0.01}
                                            onChange={(v:any) => updateInput(selectedIndex, { alpha: v })}
                                        />
                                        <div className="flex flex-col gap-2">
                                            <span className="text-[10px] font-bold text-slate-500 uppercase text-center">Layer Order</span>
                                            <div className="flex gap-2">
                                                <button onClick={() => changeLayer(selectedIndex, -1)} className="bg-slate-700 hover:bg-slate-600 px-4 rounded text-xs">-</button>
                                                <span className="flex-1 text-center font-mono text-emerald-400 py-1 bg-black/20 rounded border border-slate-700/30">
                        {selectedInput.layer}
                    </span>
                                                <button onClick={() => changeLayer(selectedIndex, 1)} className="bg-slate-700 hover:bg-slate-600 px-4 rounded text-xs">+</button>
                                            </div>
                                        </div>

                                        <div className="h-px bg-slate-700/50 col-span-2 my-1" /> {/* Separator */}

                                        <ControlSlider label="Pos X" value={selectedInput.pos_x} min={-1} max={1} step={0.001} onChange={(v:any) => updateInput(selectedIndex, { pos_x: v })} />
                                        <ControlSlider label="Pos Y" value={selectedInput.pos_y} min={-1} max={1} step={0.001} onChange={(v:any) => updateInput(selectedIndex, { pos_y: v })} />
                                        <ControlSlider label="Scale X" value={selectedInput.scale_x} min={0} max={2} step={0.001} onChange={(v:any) => updateInput(selectedIndex, { scale_x: v })} />
                                        <ControlSlider label="Scale Y" value={selectedInput.scale_y} min={0} max={2} step={0.001} onChange={(v:any) => updateInput(selectedIndex, { scale_y: v })} />
                                    </div>
                                </div>
                            )}

                            {/* TAB CHROMAKEY */}
                            {activeTab === 'chroma' && (
                                <ChromaKeyControl
                                    data={selectedInput.chromakey}
                                    isPicking={isPicking}
                                    setIsPicking={setIsPicking}
                                    onUpdate={(update) => updateChroma(selectedIndex, update)}
                                />
                            )}
                            {activeTab === 'luma' && (
                                <LumaKeyControl
                                    data={selectedInput.lumakey}
                                    onUpdate={(update) => updateLuma(selectedIndex, update)}
                                />
                            )}

                            {activeTab === 'color' && (
                                <div className="flex flex-col gap-6">
                                    {/* Header con Switch e Reset */}
                                    <div className="flex justify-between items-center bg-black/40 p-3 rounded-lg border border-slate-700/50 shadow-inner">
                                        <div className="flex items-center gap-4">
                                            <div className="flex flex-col">
                                                <span className="text-[10px] font-black text-emerald-500 uppercase tracking-widest">Master Color</span>
                                                <span className="text-[9px] text-slate-500 font-bold uppercase">
                        {isColorEnabled ? 'Correction Active' : 'Bypass Mode'}
                    </span>
                                            </div>

                                            <label className="relative inline-flex items-center cursor-pointer">
                                                <input
                                                    type="checkbox"
                                                    className="sr-only peer"
                                                    checked={isColorEnabled}
                                                    onChange={(e) => updateColor(selectedIndex, { enabled: e.target.checked ? 1.0 : 0.0 })}
                                                />
                                                <div className="w-10 h-5 bg-zinc-800 rounded-full peer peer-checked:bg-emerald-600 after:content-[''] after:absolute after:top-[2px] after:left-[2px] after:bg-slate-300 after:rounded-full after:h-4 after:w-4 after:transition-all peer-checked:after:translate-x-5 peer-checked:after:bg-white"></div>
                                            </label>
                                        </div>

                                        <button
                                            onClick={() => resetColor(selectedIndex)}
                                            className="flex items-center gap-2 px-3 py-1.5 bg-slate-800 hover:bg-rose-900/40 text-slate-400 hover:text-rose-400 border border-slate-700 hover:border-rose-500/50 rounded transition-all text-[10px] font-bold uppercase tracking-tighter"
                                        >
                                            <RotateCcw size={12} /> Reset Defaults
                                        </button>
                                    </div>

                                    {/* Sliders Grid */}
                                    <div className="grid grid-cols-2 gap-x-8 gap-y-4 opacity-100 transition-opacity">
                                        {/* Brightness & Contrast */}
                                        <ControlSlider
                                            label="Brightness"
                                            value={selectedInput.color?.brightness}
                                            min={-1} max={1} step={0.01}
                                            onChange={(v:any) => updateColor(selectedIndex, { brightness: v })}
                                        />
                                        <ControlSlider
                                            label="Contrast"
                                            value={selectedInput.color?.contrast}
                                            min={0} max={2} step={0.01}
                                            onChange={(v:any) => updateColor(selectedIndex, { contrast: v })}
                                        />

                                        {/* Saturation & Hue Shift */}
                                        <ControlSlider
                                            label="Saturation"
                                            value={selectedInput.color?.saturation}
                                            min={0} max={2} step={0.01}
                                            onChange={(v:any) => updateColor(selectedIndex, { saturation: v })}
                                        />
                                        <ControlSlider
                                            label="Hue Shift"
                                            value={selectedInput.color?.hueShift}
                                            min={-180} max={180} step={1}
                                            onChange={(v:any) => updateColor(selectedIndex, { hueShift: v })}
                                        />

                                        {/* Gamma */}
                                        <ControlSlider
                                            label="Gamma"
                                            value={selectedInput.color?.gamma}
                                            min={0.1} max={3} step={0.01}
                                            onChange={(v:any) => updateColor(selectedIndex, { gamma: v })}
                                        />

                                        <div className="h-px bg-slate-800 col-span-2" />

                                        {/* Black & White Levels */}
                                        <ControlSlider
                                            label="Black Level"
                                            value={selectedInput.color?.blackLevel}
                                            min={-1} max={1} step={0.01}
                                            onChange={(v:any) => updateColor(selectedIndex, { blackLevel: v })}
                                        />
                                        <ControlSlider
                                            label="White Level"
                                            value={selectedInput.color?.whiteLevel}
                                            min={0} max={2} step={0.01}
                                            onChange={(v:any) => updateColor(selectedIndex, { whiteLevel: v })}
                                        />
                                    </div>
                                </div>
                            )}
                        </div>
                    </div>
                </div>
            </div>
        </div>
    );
}