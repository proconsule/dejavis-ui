import { useMemo } from 'react';
import {
    Folder, FileAudio, HardDrive, ArrowUpLeft, Play, ListPlus, FolderOpen
} from "lucide-react";
import {
    ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuTrigger,
} from "@/components/ui/context-menu.tsx";

interface FileEntry {
    name: string;
    path: string;
    size: string;
    type: 'file' | 'directory';
}

interface FileExplorerData {
    basePath?: string;
    absPath?: string;
    relReq: string;
    entries: FileEntry[];
}

interface FileExplorerProps {
    data: FileExplorerData;
    onNavigate: (path: string) => void;
    onFileSelect: (path: string) => void;
    onQueueAdd: (path: string) => void;
}

export function FileExplorer_Milk({ data, onNavigate, onFileSelect, onQueueAdd }: FileExplorerProps) {
    // 1. Determina il separatore in base al path radice
    const separator = useMemo(() => {
        const base = data?.absPath || data?.basePath || "";
        return base.includes('\\') ? '\\' : '/';
    }, [data?.absPath, data?.basePath]);

    if (!data) {
        return (
            <div className="p-8 text-center text-xs text-slate-400 animate-pulse">
                In attesa di dati dal sistema...
            </div>
        );
    }


    const getRelPath = () => {

        const current = data?.relReq || "";

        if (current === "" || current === "/" || current === "\\") return "";

        const normalized = current.replace(/[\\/]$/, "");

        const parts = normalized.split(/[\\/]/);
        parts.pop();

        const sep = current.includes('\\') ? '\\' : '/';
        return parts.join(sep);
    };


    return (
        <div className="bg-white rounded-xl border shadow-sm overflow-hidden flex flex-col h-[450px] select-none">
            <div className="bg-slate-50 border-b px-4 py-2 flex items-center gap-2 text-xs font-mono text-slate-500">
                <HardDrive className="h-3.5 w-3.5" />
                <span className="truncate opacity-50">{data.absPath || data.basePath}</span>
                <span className="truncate font-bold text-purple-700 italic">
         {separator} {data.relReq}
        </span>
            </div>

            <div className="flex-1 overflow-y-auto">
                <table className="w-full text-left border-collapse">
                    <tbody className="divide-y divide-slate-50">
                    {data.absPath !== "" && (
                        <tr
                            onDoubleClick={() => onNavigate(getRelPath() ?? "")}
                            className="hover:bg-slate-100/50 cursor-pointer group transition-colors"
                        >
                            <td colSpan={2} className="px-4 py-2 flex items-center gap-3 text-slate-400 italic text-sm">
                                <ArrowUpLeft className="h-4 w-4 group-hover:-translate-y-0.5 group-hover:-translate-x-0.5 transition-transform" />
                                .. (Sali di livello)
                            </td>
                        </tr>
                    )}

                    {data.entries?.map((entry, idx) => (
                        <ContextMenu key={`${entry.path}-${idx}`}>
                            <ContextMenuTrigger asChild>
                                <tr
                                    onDoubleClick={() => entry.type === 'directory' ? onNavigate(entry.path) : onFileSelect(entry.path)}
                                    className="group hover:bg-purple-50/50 cursor-pointer transition-colors"
                                >
                                    <td className="px-4 py-2 flex items-center gap-3">
                                        {entry.type === 'directory' ? (
                                            <Folder className="text-amber-500 h-4 w-4 fill-amber-500/10" />
                                        ) : (
                                            <FileAudio className="text-purple-500 h-4 w-4" />
                                        )}
                                        <span className="text-sm font-medium text-slate-700 truncate">
                        {entry.name}
                      </span>
                                    </td>
                                    <td className="px-4 py-2 text-right text-xs font-mono text-slate-400">
                                        {entry.type === 'file' ? entry.size : '--'}
                                    </td>
                                </tr>
                            </ContextMenuTrigger>

                            <ContextMenuContent className="w-48">
                                <ContextMenuItem onClick={() => entry.type === 'directory' ? onNavigate(entry.path) : onFileSelect(entry.path)}>
                                    {entry.type === 'directory' ? (
                                        <>
                                            <FolderOpen className="mr-2 h-4 w-4" />
                                            Apri cartella
                                        </>
                                    ) : (
                                        <>
                                            <Play className="mr-2 h-4 w-4" />
                                            Riproduci traccia
                                        </>
                                    )}
                                </ContextMenuItem>

                                {entry.type === 'file' && (
                                    <ContextMenuItem onClick={() => onQueueAdd(entry.path)}>
                                        <ListPlus className="mr-2 h-4 w-4 text-purple-600" />
                                        <span className="text-purple-600 font-medium">Aggiungi alla coda</span>
                                    </ContextMenuItem>
                                )}
                            </ContextMenuContent>
                        </ContextMenu>
                    ))}

                    {data.entries?.length === 0 && (
                        <tr>
                            <td colSpan={2} className="px-4 py-8 text-center text-sm text-slate-400">
                                Questa cartella è vuota
                            </td>
                        </tr>
                    )}
                    </tbody>
                </table>
            </div>
        </div>
    );
}