import Editor from '@monaco-editor/react';
import { Card, CardContent, CardHeader } from "@/components/ui/card"
import { Button } from "@/components/ui/button"
import { Code2, RefreshCw, Save, Play, Send } from "lucide-react"

interface ShaderEditorProps {
    code: string;
    setCode: (code: string) => void;
    currentShader: { id: number, name: string } | null;
    error: string | null;
    canSave: boolean;
    isFetching: boolean;
    onTest: () => void;
    onDeploy: () => void;
    onSave: () => void;
    onRefresh: () => void;
}

export function ShaderEditor({
                                 code, setCode, currentShader, error, canSave, isFetching,
                                 onTest, onDeploy, onSave, onRefresh
                             }: ShaderEditorProps) {

    const handleEditorWillMount = (monaco: any) => {
        monaco.editor.defineTheme('dracula', {
            base: 'vs-dark',
            inherit: true,
            rules: [
                { token: 'comment', foreground: '6272a4', fontStyle: 'italic' },
                { token: 'keyword', foreground: 'ff79c6' },
                { token: 'number', foreground: 'bd93f9' },
                { token: 'type', foreground: '8be9fd' },
                { token: 'operators', foreground: 'ff79c6' },
                { token: 'identifier', foreground: 'f8f8f2' },
                { token: 'function', foreground: '50fa7b' },
            ],
            colors: {
                'editor.background': '#282a36',
                'editor.foreground': '#f8f8f2',
                'editor.lineHighlightBackground': '#44475a',
                'editor.selectionBackground': '#44475a',
                'editorCursor.foreground': '#f8f8f0',
            }
        });
    };

    return (
        <Card className="flex-1 flex flex-col bg-[#1e1f29] border-zinc-800 overflow-hidden shadow-2xl border-none h-full">
            <CardHeader className="flex flex-row items-center justify-between py-2 px-4 border-b border-zinc-900 bg-[#282a36] shrink-0">
                <div className="flex flex-col min-w-0">
                    <div className="flex items-center gap-2 font-bold text-zinc-500 text-[10px] uppercase tracking-widest">
                        <Code2 className="h-3.5 w-3.5 text-purple-400" />
                        Monaco Engine
                    </div>
                    {currentShader && (
                        <span className="text-[10px] text-purple-300 font-mono italic mt-0.5 truncate">
                            {currentShader.name}
                        </span>
                    )}
                </div>

                <div className="flex gap-2 shrink-0">
                    <Button variant="ghost" size="sm" className="h-8 w-8 p-0 text-zinc-400" onClick={onRefresh} disabled={isFetching}>
                        <RefreshCw className={`h-3.5 w-3.5 ${isFetching ? 'animate-spin' : ''}`} />
                    </Button>

                    {currentShader && canSave && (
                        <Button size="sm" className="h-8 bg-green-600 hover:bg-green-500 text-white font-bold" onClick={onSave}>
                            <Save className="h-3.5 w-3.5 mr-2" /> SAVE
                        </Button>
                    )}

                    <Button variant="secondary" size="sm" className="h-8 bg-zinc-700 text-[11px] font-bold text-white px-4" onClick={onTest}>
                        <Play className="h-3.5 w-3.5 mr-1.5 fill-current" /> TEST
                    </Button>

                    <Button size="sm" className="h-8 bg-purple-600 hover:bg-purple-500 text-[11px] font-bold text-white px-4" onClick={onDeploy}>
                        <Send className="h-3.5 w-3.5 mr-1.5" /> DEPLOY
                    </Button>
                </div>
            </CardHeader>

            <CardContent className="p-0 flex-1 relative bg-[#282a36]">
                <Editor
                    height="100%"
                    defaultLanguage="cpp" // Usiamo C++ perché la sintassi GLSL è quasi identica
                    theme="dracula"
                    value={code}
                    beforeMount={handleEditorWillMount}
                    onChange={(value) => setCode(value || "")}
                    options={{
                        minimap: { enabled: false },
                        fontSize: 14,
                        fontFamily: '"JetBrains Mono", "Fira Code", monospace',
                        lineNumbers: "on",
                        roundedSelection: true,
                        scrollBeyondLastLine: false,
                        readOnly: false,
                        automaticLayout: true, // Fondamentale per il resize della dashboard
                        padding: { top: 20 },
                        cursorSmoothCaretAnimation: "on",
                        smoothScrolling: true,
                        contextmenu: true,
                    }}
                />

                {/* Box Errori dinamico (Overlay sopra Monaco) */}
                {error && (
                    <div className="absolute bottom-6 left-6 right-6 p-4 bg-[#ff5555]/90 backdrop-blur-md border border-white/20 rounded-lg shadow-2xl z-50 animate-in slide-in-from-bottom-4">
                        <div className="text-[9px] font-black text-white uppercase mb-1 tracking-tighter">Compiler Feedback</div>
                        <pre className="text-[11px] font-mono text-white whitespace-pre-wrap leading-tight">{error}</pre>
                    </div>
                )}
            </CardContent>
        </Card>
    );
}