import { Loader2 } from "lucide-react"

interface ConnectionOverlayProps {
  state: string;
}

export function ConnectionOverlay({ state }: ConnectionOverlayProps) {
    return (
        <div className="absolute inset-0 z-[100] flex items-center justify-center bg-slate-900/60">

            <div className="flex flex-col items-center gap-4 p-6 bg-white rounded-lg border-2 border-orange-500 shadow-none">

                <div className="relative">
                    <Loader2 className="h-8 w-8 animate-spin text-orange-500 stroke-[2px]" />
                </div>

                <div className="text-center">
                    <h2 className="text-sm font-black uppercase tracking-tighter text-slate-900">
                        DEJAVISUI Backend Offline
                    </h2>
                    <p className="text-[10px] font-mono font-bold text-orange-600">
                        {state}
                    </p>
                </div>

                <div className="w-full h-1 bg-slate-100 rounded-full overflow-hidden">
                    <div className="h-full bg-orange-500 w-1/3 animate-[pulse_1.5s_ease-in-out_infinite]" />
                </div>
            </div>
        </div>
    );
}