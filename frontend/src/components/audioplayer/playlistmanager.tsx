import { useState } from 'react'
import { ScrollArea } from "@/components/ui/scroll-area"
import { Button } from "@/components/ui/button"
import { Play, Trash2, ListMusic, Activity } from "lucide-react"
import { Marquee } from "@/components/ui/marquee"
import {
  ContextMenu,
  ContextMenuContent,
  ContextMenuItem,
  ContextMenuTrigger,
} from "@/components/ui/context-menu"

interface PlaylistEntry {
  id: number;
  fileName: string;
  title: string;
  artist: string;
  duration: number;
  isMetadataLoaded: boolean;
}

interface PlaylistManagerProps {
  queue: PlaylistEntry[];
  currentTrackId: number;
  status?: any;
  sendSignal: (command: any) => void;
}

export function PlaylistManager({ queue, currentTrackId, status, sendSignal }: PlaylistManagerProps) {
  const [hoveredId, setHoveredId] = useState<number | null>(null);

  const formatTime = (s: number) => {
    if (!s || s <= 0) return "00:00";
    const mins = Math.floor(s / 60);
    const secs = Math.floor(s % 60);
    return `${mins}:${secs.toString().padStart(2, '0')}`;
  };

  const progress = status?.audio?.currentTime && status?.audio?.duration 
    ? (status.audio.currentTime / status.audio.duration) * 100 
    : 0;

  return (
    <div className="flex flex-col h-full bg-[#0a0a0c] rounded-xl border border-white/5 overflow-hidden shadow-inner">
      
      {/* Header compatto */}
      <div className="p-3 border-b border-white/[0.03] flex items-center justify-between bg-white/[0.01]">
        <div className="flex items-center gap-2">
          <ListMusic className="h-3.5 w-3.5 text-purple-500" />
          <span className="text-[10px] font-bold uppercase tracking-widest text-slate-500">Queue</span>
        </div>
        <span className="font-mono text-[9px] text-slate-600">{queue.length} UNITS</span>
      </div>

      <ScrollArea className="flex-1">
        <div className="p-1.5 space-y-0.5">
          {queue.map((track, index) => {
            const isActive = track.id === currentTrackId;
            const isHovered = hoveredId === track.id;
            const displayName = track.title || track.fileName;
            
            return (
              <ContextMenu key={track.id}>
                <ContextMenuTrigger>
                  <div
                    onMouseEnter={() => setHoveredId(track.id)}
                    onMouseLeave={() => setHoveredId(null)}
                    className={`relative flex items-center gap-3 p-2 rounded-lg transition-all group ${
                      isActive 
                        ? 'bg-purple-500/10 border border-purple-500/20' 
                        : 'hover:bg-white/[0.03] border border-transparent'
                    }`}
                  >
                    {/* Status Icon */}
                    <div className="w-4 shrink-0 flex justify-center">
                      {isActive ? (
                        <Activity className="h-3 w-3 text-purple-400 animate-pulse" />
                      ) : (
                        <span className="text-[9px] font-mono text-slate-700 group-hover:text-slate-400">
                          {(index + 1).toString().padStart(2, '0')}
                        </span>
                      )}
                    </div>

                    {/* Info con Marquee */}
                    <div 
                      className="flex-1 min-w-0 cursor-pointer" 
                      onDoubleClick={() => sendSignal({ command: "play_by_id", id: track.id })}
                    >
                      <div className="h-5 flex items-center w-full overflow-hidden">
                        {isHovered ? (
                          <Marquee speed="fast" pauseOnHover={false} className="text-sm">
                            <span className={isActive ? 'text-white font-bold' : 'text-slate-300'}>
                              {displayName}
                            </span>
                          </Marquee>
                        ) : (
                          <div className={`text-sm truncate w-full ${isActive ? 'text-white font-bold' : 'text-slate-400'}`}>
                            {displayName}
                          </div>
                        )}
                      </div>
                      <div className="text-[9px] truncate uppercase tracking-tighter text-slate-600">
                        {track.artist || "Unknown Source"}
                      </div>
                    </div>

                    {/* Meta & Quick Actions */}
                    <div className="flex items-center gap-2 shrink-0">
                      <span className={`font-mono text-[9px] transition-opacity ${isHovered ? 'opacity-0' : 'opacity-100'} ${isActive ? 'text-purple-400' : 'text-slate-700'}`}>
                        {formatTime(track.duration)}
                      </span>

                      <div className={`absolute right-2 flex gap-1 transition-opacity ${isHovered ? 'opacity-100' : 'opacity-0'}`}>
                        <Button
                          size="icon"
                          variant="ghost"
                          className="h-6 w-6 rounded bg-[#121214] hover:bg-purple-600 hover:text-white border border-white/5"
                          onClick={() => sendSignal({ command: "play_by_id", id: track.id })}
                        >
                          <Play className="h-2.5 w-2.5 fill-current" />
                        </Button>
                      </div>
                    </div>

                    {/* Progress Bar (1px) */}
                    {isActive && (
                      <div className="absolute bottom-0 left-0 h-[1.5px] bg-purple-500/10 w-full overflow-hidden">
                        <div 
                          className="h-full bg-purple-500 shadow-[0_0_8px_#a855f7]" 
                          style={{ width: `${progress}%`, transition: 'width 1s linear' }} 
                        />
                      </div>
                    )}
                  </div>
                </ContextMenuTrigger>

                <ContextMenuContent className="w-48 bg-[#121214] border-white/10 text-slate-300">
                  <ContextMenuItem 
                    onClick={() => sendSignal({ command: "play_by_id", id: track.id })}
                    className="gap-2 focus:bg-purple-600 focus:text-white"
                  >
                    <Play className="h-3.5 w-3.5" /> Play Now
                  </ContextMenuItem>
                  <ContextMenuItem 
                    onClick={() => sendSignal({ command: "remove_track", id: track.id })}
                    className="gap-2 text-red-400 focus:bg-red-900/50 focus:text-red-200"
                  >
                    <Trash2 className="h-3.5 w-3.5" /> Delete from Queue
                  </ContextMenuItem>
                </ContextMenuContent>
              </ContextMenu>
            );
          })}
        </div>
      </ScrollArea>
    </div>
  )
}