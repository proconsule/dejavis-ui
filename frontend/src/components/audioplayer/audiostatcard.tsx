import { type LucideIcon } from "lucide-react";

interface AudioStatCardProps {
  icon: LucideIcon;
  iconColor: string; // Ignorato internamente per ora, o usato solo per l'icona
  label: string;
  value: string | number;
  unit: string;
}

export function AudioStatCard({ icon: Icon, label, value, unit }: AudioStatCardProps) {
  return (
    <div className="bg-black border border-white/20 rounded-lg px-3 py-2 flex flex-col justify-center min-h-[60px]">
      <div className="flex items-center gap-1.5 mb-0.5 opacity-60">
        <Icon className="h-2.5 w-2.5 text-white" />
        <span className="text-[8px] font-black uppercase tracking-widest text-white">
          {label}
        </span>
      </div>
      
      <div className="flex items-baseline gap-1">
        <span className="text-xl font-black font-mono leading-none tracking-tighter text-white">
          {value}
        </span>
        <span className="text-[9px] font-bold text-white/50 uppercase">
          {unit}
        </span>
      </div>
    </div>
  )
}