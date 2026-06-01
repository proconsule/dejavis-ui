import { Card, CardContent } from "@/components/ui/card"
import { Badge } from "@/components/ui/badge"
import { Progress } from "@/components/ui/progress"
import { Music, Waves, Zap, FileAudio } from "lucide-react"
import { Marquee } from "@/components/ui/marquee"

interface AudioPlayerCardProps {
  fileData: {
    title: string;
    filename: string;
    position: number;
    duration: number;
    isPlaying: boolean;
    codecName: string;
    bitrate: number;
    sampleRate: number;
  };
  decoderBuffer: {
    fill: number;
    max: number;
  };
  onDoubleClick?: () => void; // Aggiungi qui
}

export function AudioPlayerCard({ fileData, decoderBuffer,onDoubleClick}: AudioPlayerCardProps) {
  const formatTime = (s: number) => {
    if (!s || s < 0) return "00:00";
    const mins = Math.floor(s / 60);
    const secs = Math.floor(s % 60);
    return `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
  };

  const progressPercent = (fileData?.duration > 0) ? (fileData.position / fileData.duration) * 100 : 0;
  const kbps = fileData?.bitrate ? Math.round(fileData.bitrate / 1000) : 0;
  // Estrae il nome del file dal percorso completo
  const fileNameOnly = fileData?.filename?.split(/[\\/]/).pop() || "No file";

  return (
    <Card className="border-2 border-slate-100 shadow-sm hover:border-purple-200 transition-colors" onDoubleClick={onDoubleClick}>
	  <CardContent className="pt-6">
        {/* HEADER: Icona e Stato */}
        <div className="flex items-center justify-between mb-4">
          <div className="p-2 rounded-lg border text-purple-600 bg-purple-50 border-purple-100">
            <Music className="h-5 w-5" />
          </div>
          <div className="flex gap-1.5 items-center">
            <Badge variant="outline" className="text-[9px] font-mono border-purple-200 text-purple-700 bg-purple-50/50 uppercase">
              {fileData?.codecName?.split(' ')[0] || "N/A"}
            </Badge>
            <Badge className={fileData?.isPlaying ? "bg-purple-600 animate-pulse" : "bg-slate-200 text-slate-500"}>
              {fileData?.isPlaying ? "LIVE" : "OFF"}
            </Badge>
          </div>
        </div>

        {/* INFO TRACCIA E FILE */}
        <div className="mb-4">
          <p className="text-[10px] font-black text-slate-400 uppercase tracking-widest">Audio Player</p>
          <h3 className="text-sm font-bold truncate text-slate-800 mt-1 uppercase">
            {fileData?.title || "Unknown Title"}
          </h3>
          <div className="flex items-center gap-1.5 mt-1 text-[10px] text-slate-500 font-mono overflow-hidden">
  <FileAudio className="h-3 w-3 flex-shrink-0" />
  <Marquee speed="slow" className="w-full">
    {fileNameOnly}
  </Marquee>
</div>
          <div className="flex items-center gap-3 mt-1 text-[9px] font-bold text-muted-foreground uppercase">
            <span className="flex items-center gap-1"><Zap className="h-2.5 w-2.5" />{kbps} kbps</span>
            <span>{fileData?.sampleRate ? `${fileData.sampleRate / 1000}kHz` : ""}</span>
          </div>
        </div>

        {/* PROGRESSO E BUFFER */}
        <div className="space-y-3">
          <div className="space-y-1">
            <div className="flex justify-between text-[10px] font-mono font-bold text-purple-600">
              <span>{formatTime(fileData?.position)}</span>
              <span>{formatTime(fileData?.duration)}</span>
            </div>
            <Progress value={progressPercent} className="h-1.5 bg-purple-100" />
          </div>

          <div className="pt-2 border-t border-slate-50">
            <div className="flex justify-between items-center text-[9px] font-mono text-slate-400 uppercase font-bold">
              <div className="flex items-center gap-1">
                <Waves className="h-3 w-3 text-cyan-500" />
                <span>Decoder Fill</span>
              </div>
              <span>{decoderBuffer.fill} smp</span>
            </div>
            <Progress value={(decoderBuffer.fill / (decoderBuffer.max || 16384)) * 100} className="h-1 mt-1" />
          </div>
        </div>
      </CardContent>
    </Card>
  );
}