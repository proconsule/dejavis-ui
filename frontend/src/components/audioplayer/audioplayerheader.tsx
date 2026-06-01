import { Badge } from "@/components/ui/badge"
import { Marquee } from "@/components/ui/marquee"
import { FileAudio } from "lucide-react"

interface AudioPlayerHeaderProps {
  title?: string;
  filename?: string;
  codec?: string;
}

export function AudioPlayerHeader({ title, filename, codec }: AudioPlayerHeaderProps) {
  const cleanTitle = title?.trim();

  const getDisplayName = (path?: string) => {
    if (!path) return "";
    return path.split(/[\\/]/).pop() || path;
  };

  const displayTitle =
      cleanTitle && cleanTitle !== ""
          ? cleanTitle
          : getDisplayName(filename) || "NO TRACK LOADED";

  const codecLabel = codec?.split(" ")[0] || "---";

  return (
      <div className="flex items-center gap-3 px-1 py-0.5">
        {/* Icon */}
        <FileAudio className="h-4 w-4 text-purple-400 flex-shrink-0 opacity-70" />

        {/* Title + path stacked, fills remaining space */}
        <div className="flex-1 min-w-0 overflow-hidden">
          {/* Track title */}
          <Marquee
              speed="slow"
              className="text-sm font-semibold tracking-wide uppercase text-purple-300 leading-tight"
          >
            {displayTitle}
            <span className="mx-10" />
          </Marquee>

          {/* File path — single line, muted */}
          <div className="overflow-hidden">
            <Marquee speed="fast" className="font-mono text-[10px] text-slate-500 leading-tight mt-0.5">
              {filename || "Waiting for backend…"}
              <span className="mx-10" />
            </Marquee>
          </div>
        </div>

        {/* Codec badge — compact, right-aligned */}
        <Badge
            variant="outline"
            className="border-purple-500/40 text-purple-300 bg-purple-500/10 text-[10px] font-mono px-2 py-0.5 shrink-0 tracking-widest uppercase"
        >
          {codecLabel}
        </Badge>
      </div>
  );
}
