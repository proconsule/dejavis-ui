import { Waves, Clock } from "lucide-react"

interface AudioBufferCardProps {
  fill: number;
  max: number;
  label: string;
  sampleRate?: number; // Aggiunto
  channels?: number;   // Aggiunto
  className?: string;
}

export function AudioBufferCard({ 
  fill, 
  max, 
  label, 
  sampleRate = 44100, 
  channels = 2, 
  className 
}: AudioBufferCardProps) {
  const percentage = (fill / max) * 100;

  // Calcolo del tempo in millisecondi
  // fill / (sampleRate * channels) ci dà i secondi, poi * 1000 per i ms
  const ms = (fill / (sampleRate * channels)) * 1000;

  return (
    <div className={`bg-black border border-white/20 rounded-lg px-3 py-2 flex flex-col justify-center min-h-[70px] ${className}`}>
      
      {/* Header: Label e Tempo in MS */}
      <div className="flex items-center justify-between mb-2">
        <div className="flex items-center gap-1.5 opacity-60">
          <Waves className="h-2.5 w-2.5 text-white" />
          <span className="text-[8px] font-black uppercase tracking-widest text-white">
            {label}
          </span>
        </div>
        
        <div className="flex items-center gap-1.5">
           {/* Badge dei Millisecondi */}
           <div className="flex items-center gap-1 bg-white/10 px-1.5 py-0.5 rounded text-[9px] font-mono font-bold text-white/80">
            <Clock className="h-2 w-2" />
            {ms.toFixed(1)} ms
          </div>
          <span className="font-mono text-[10px] font-black text-white w-8 text-right">
            {Math.round(percentage)}%
          </span>
        </div>
      </div>
      
      {/* Container della Barra */}
      <div className="h-1.5 w-full bg-white/10 rounded-full overflow-hidden">
        <div 
          className="h-full bg-white transition-all duration-300 ease-out shadow-[0_0_8px_rgba(255,255,255,0.3)]"
          style={{ width: `${Math.min(percentage, 100)}%` }}
        />
      </div>

      {/* Footer Opzionale: Info tecnica piccola */}
      <div className="mt-1.5 flex justify-between items-center opacity-30">
         <span className="text-[7px] font-mono text-white italic">{fill} / {max} samples</span>
      </div>
    </div>
  )
}