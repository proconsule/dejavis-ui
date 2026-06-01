import { useEffect, useState } from 'react'
import { Card, CardContent, CardHeader } from "@/components/ui/card"
import { Monitor, Activity } from "lucide-react"
import { ShaderView } from "@/components/shader/shader-view.tsx"
import { useWS } from '@/WebSocketContext'
import { ShaderBrowser } from "@/components/shader/shaderbrowser.tsx"
import { ShaderEditor } from "@/components/shader/shader-editor.tsx"

const DEFAULT_SHADER = `void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    vec2 uv = fragCoord/iResolution.xy;
    vec3 col = 0.5 + 0.5*cos(iTime + uv.xyx + vec3(0,2,4));
    fragColor = vec4(col,1.0);
}`;

export function ShaderDashboard() {
    const { sendMessage, lastJsonMessage } = useWS();

    const [isFetching, setIsFetching] = useState(false);
    const [code, setCode] = useState(DEFAULT_SHADER);
    const [compiledCode, setCompiledCode] = useState(DEFAULT_SHADER);
    const [canSave, setCanSave] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [stats, setStats] = useState({ time: 0, frame: 0, fps: 0 });

    const [currentShader, setCurrentShader] = useState<{ id: number, name: string } | null>(null);

    const handleSelectShader = (id: number, name: string) => {
        setCurrentShader({ id, name });
        setIsFetching(true);
        setError(null);
        sendMessage({ msgid: 71, shader_id: id });
    };

    const handleCodeChange = (newCode: string) => {
        setCode(newCode);
        setCanSave(false); // Reset salvataggio se il codice viene sporcato
    };

    const handleSave = () => {
        if (!currentShader) return;
        try {
            const b64 = btoa(code);
            sendMessage({
                msgid: 76,
                shader_id: currentShader.id,
                source_b64: b64
            });
        } catch (e) {
            setError("Errore codifica Base64 per salvataggio");
        }
    };

    const handleTest = () => {
        try {
            const b64 = btoa(code);
            sendMessage({ msgid: 62, source_b64: b64 });
        } catch (e) {
            setError("Errore codifica Base64 per test");
        }
    };

    const handleDeploy = () => {
        try {
            const b64 = btoa(code);
            sendMessage({ msgid: 60, source_b64: b64 });
        } catch (e) {
            setError("Errore codifica Base64 per deploy");
        }
    };

    const handleGetCurrent = () => {
        setIsFetching(true);
        sendMessage({ msgid: 61, timestamp: Date.now() });
    };


    useEffect(() => {
        if (!lastJsonMessage) return;

        // 1. Caricamento codice (da hardware o da libreria)
        if (lastJsonMessage.msgid === 61 || lastJsonMessage.msgid === 71) {
            const b64Field = lastJsonMessage.msgid === 61 ? lastJsonMessage.current_b64 : lastJsonMessage.source_b64;

            if (b64Field) {
                try {
                    const decodedCode = atob(b64Field);
                    setCode(decodedCode);
                    setCompiledCode(decodedCode);
                    setError(null);
                    setCanSave(false);
                } catch (e) {
                    setError("Errore durante la decodifica del codice ricevuto");
                }
            }
            setIsFetching(false);
        }

        // 2. Risultato del Test di compilazione (62)
        if (lastJsonMessage.msgid === 62) {
            if (lastJsonMessage.success) {
                setCanSave(true); // Compilazione OK: sblocca il tasto SAVE
                const decodedCode = atob(lastJsonMessage.source_b64);
                setCompiledCode(decodedCode); // Aggiorna la preview WebGL
                setError(null);
            } else {
                setCanSave(false);
                setError(lastJsonMessage.errormsg || "Errore di compilazione generico");
            }
        }

        // 3. Risposta dal Database per salvataggio (76)
        if (lastJsonMessage.msgid === 76) {
            if (lastJsonMessage.success) {
                setCanSave(false); // Nascondi il tasto SAVE dopo successo
                console.log("Shader salvato nel DB");
            } else {
                setError("Salvataggio fallito: " + (lastJsonMessage.msg || "Errore DB"));
            }
        }
    }, [lastJsonMessage]);

    return (
        <div className="flex h-screen bg-black overflow-hidden p-4 gap-4 text-white">
            {/* STILI SCROLLBAR GLOBALI PER LA DASHBOARD */}
            <style dangerouslySetInnerHTML={{ __html: `
                .force-scrollbar::-webkit-scrollbar { width: 8px !important; display: block !important; }
                .force-scrollbar::-webkit-scrollbar-track { background: #050505 !important; }
                .force-scrollbar::-webkit-scrollbar-thumb { background: #333 !important; border-radius: 4px; }
                .force-scrollbar::-webkit-scrollbar-thumb:hover { background: #444 !important; }
                .no-scrollbar::-webkit-scrollbar { display: none; }
            `}} />

            {/* --- COLONNA SINISTRA: PREVIEW + FILE BROWSER --- */}
            <div className="flex flex-col w-[450px] gap-4 shrink-0 h-full">

                {/* AREA PREVIEW VULKAN/WEBGL */}
                <Card className="bg-zinc-950 border-zinc-800 overflow-hidden relative shadow-2xl border-none aspect-square shrink-0">
                    <div className="absolute top-14 left-4 z-10 flex flex-row gap-2 pointer-events-none font-mono text-[9px]">
                        <div className="bg-black/80 backdrop-blur-md px-2 py-1 rounded border border-white/5 flex items-center gap-2">
                            <Activity className="h-3 w-3 text-green-500" />
                            <span className="text-white font-bold">{stats.fps} FPS</span>
                        </div>
                    </div>

                    <CardHeader className="py-2 px-4 border-b border-zinc-800 bg-zinc-900/50 relative z-20">
                        <div className="flex items-center gap-2 font-bold text-white">
                            <Monitor className="h-3 w-3 text-amber-500" />
                            <span className="text-[9px] uppercase tracking-widest">Visual Output</span>
                        </div>
                    </CardHeader>

                    <CardContent className="p-0 bg-black h-full flex items-center justify-center relative">
                        <ShaderView
                            compiledCode={compiledCode}
                            //audioData={audioLevels}
                            onError={setError}
                            onStats={setStats}
                        />
                    </CardContent>
                </Card>

                <div className="flex-1 min-h-0 border border-zinc-800 rounded-xl overflow-hidden bg-zinc-950 shadow-xl">
                    <ShaderBrowser onSelectShader={handleSelectShader} />
                </div>
            </div>

            <div className="flex-1 min-w-0 h-full">
                <ShaderEditor
                    code={code}
                    setCode={handleCodeChange}
                    currentShader={currentShader}
                    error={error}
                    canSave={canSave}
                    isFetching={isFetching}
                    onTest={handleTest}
                    onDeploy={handleDeploy}
                    onSave={handleSave}
                    onRefresh={handleGetCurrent}
                    //audioData={audioData}
                />
            </div>
        </div>
    );
}