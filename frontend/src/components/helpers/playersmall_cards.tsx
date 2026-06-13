import { Card, CardContent } from "@/components/ui/card"
import { Badge } from "@/components/ui/badge"
import { Marquee } from "@/components/ui/marquee"
import { cn } from "@/lib/utils.ts";
import { Music, Film } from "lucide-react"
import { useWS } from "@/WebSocketContext.tsx"
import { useEffect, useState } from "react"

interface AudioPlayerCardProps {
    audioIdx: number;
    video_mixer_idx: number;
    onHit: () => void;
}

export function Playersmall_Card({ audioIdx, video_mixer_idx, onHit }: AudioPlayerCardProps) {
    const { lastJsonMessage } = useWS();
    const [source, setSource] = useState<any>(null);
    const [isPlaying, setIsPlaying] = useState(false);
    const [progress, setProgress] = useState(0);

    const formatTime = (s: number) => {
        if (!s || s < 0) return "00:00";
        const mins = Math.floor(s / 60);
        const secs = Math.floor(s % 60);
        return `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
    };

    console.log(source)

    const getTrackDisplay = () => {
        const isVideo = lastJsonMessage?.audio?.mixer?.inputs[audioIdx]?.type === 3;

        if (isVideo) {
            if (!source?.filename) return "No video loaded...";
            return source.filename.split(/[\\\/]/).pop() || "Unknown Video";
        }

        if (source?.title) {
            return `${source.title} — ${source.artist || "Unknown Artist"}`;
        }

        if (source?.filename) {
            return source.filename.split(/[\\\/]/).pop() || "Unknown Audio";
        }

        return "No audio loaded...";
    };

    useEffect(() => {
        if (!lastJsonMessage) return;

        const input = lastJsonMessage.audio?.mixer?.inputs[audioIdx];
        if (!input) return;

        const isVideo = input.type === 3;
        const decoder = isVideo
            ? lastJsonMessage.video?.videomixer?.inputs[video_mixer_idx]?.file_decoder
            : input.fileplayer;

        setSource(decoder);
        setIsPlaying(decoder ? decoder.isPlaying : false);
        setProgress(decoder?.duration > 0 ? (decoder.position / decoder.duration) * 100 : 0);
    }, [lastJsonMessage, audioIdx, video_mixer_idx]);

    return (
        <Card
            className="group overflow-hidden border-2 border-slate-800 bg-zinc-900 transition-all hover:border-purple-500/50 cursor-pointer"
            onClick={onHit}
        >
            <CardContent className="p-3">
                <div className="flex items-center justify-between mb-3">
                    <div className="flex items-center gap-2">
                        <div className={cn(
                            "h-2 w-2 rounded-full shadow-[0_0_4px_inherit]",
                            isPlaying ? "bg-green-500 animate-pulse" : "bg-slate-600"
                        )} />
                        <span className="text-[10px] font-bold uppercase tracking-widest text-slate-400">
                            CH {audioIdx}
                        </span>
                        <div className="p-1 rounded border border-white/10 bg-white/5 text-white/40">
                            {lastJsonMessage?.audio?.mixer?.inputs[audioIdx]?.type === 3 ? <Film className="h-3 w-3" /> : <Music className="h-3 w-3" />}
                        </div>
                    </div>
                    <Badge className={cn("text-[8px] h-4 px-1 border font-mono",
                        isPlaying ? "bg-purple-500/10 text-purple-400 border-purple-500/30" : "text-slate-500 border-slate-700")}>
                        {isPlaying ? "PLAYING" : "IDLE"}
                    </Badge>
                </div>

                <div className="flex flex-col gap-1.5">
                    <div className="min-h-[20px] flex items-center">
                        <Marquee speed="slow" className="text-xs font-medium text-slate-200 truncate italic">
                            {getTrackDisplay()}
                            <span className="mx-8" />
                        </Marquee>
                    </div>

                    <div className="grid grid-cols-2 gap-2 items-center">
                        <div className="h-1 w-full bg-slate-800 rounded-full overflow-hidden">
                            <div
                                className="h-full bg-purple-500 transition-all duration-300"
                                style={{ width: `${progress}%` }}
                            />
                        </div>
                        <div className="flex justify-end gap-1.5 text-[9px] font-mono text-slate-500">
                            <span>{formatTime(source?.position || 0)}</span>
                            <span>/</span>
                            <span>{formatTime(source?.duration || 0)}</span>
                        </div>
                    </div>
                </div>
            </CardContent>
        </Card>
    );
}