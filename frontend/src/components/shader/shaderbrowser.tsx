import React, { useState, useEffect, useRef } from 'react';
import {
    Folder,
    ChevronLeft,
    Trash2,
    Cuboid,
    FolderPlus,
    FilePlus,
    Edit2,
    Move,
    ExternalLink
} from 'lucide-react';
import { useWS } from '@/WebSocketContext';

interface FileItem {
    id: number;
    name: string;
    type: 'folder' | 'shader';
}

export function ShaderBrowser({ onSelectShader }: { onSelectShader: (id: number, name: string) => void }) {
    const { sendMessage, lastJsonMessage } = useWS();
    const [items, setItems] = useState<FileItem[]>([]);
    const [currentFolderId, setCurrentFolderId] = useState<number>(0);
    const [history, setHistory] = useState<number[]>([]);

    // Ref per il calcolo della posizione relativa del menu rispetto al container
    const browserRef = useRef<HTMLDivElement>(null);
    const [contextMenu, setContextMenu] = useState<{ x: number, y: number, item: FileItem } | null>(null);

    // 1. Caricamento dati
    useEffect(() => {
        sendMessage({ msgid: 70, folder_id: currentFolderId });
    }, [currentFolderId, sendMessage]);

    useEffect(() => {
        if (lastJsonMessage?.msgid === 70) {
            const folders = (lastJsonMessage.folders || []).map((f: any) => ({ ...f, type: 'folder' }));
            const shaders = (lastJsonMessage.shaders || []).map((s: any) => ({ ...s, type: 'shader' }));
            setItems([...folders, ...shaders]);
        }
    }, [lastJsonMessage]);

    // 2. Chiusura automatica menu
    useEffect(() => {
        const close = () => setContextMenu(null);
        window.addEventListener('click', close);
        return () => window.removeEventListener('click', close);
    }, []);

    // 3. Gestione Context Menu con Collision Detection
    const handleContextMenu = (e: React.MouseEvent, item: FileItem) => {
        e.preventDefault();
        if (!browserRef.current) return;

        const rect = browserRef.current.getBoundingClientRect();
        const menuWidth = 208;  // w-52 = 13rem = 208px
        const menuHeight = 240; // Altezza stimata massima

        let x = e.clientX - rect.left;
        let y = e.clientY - rect.top;

        // Se il menu esce a destra, lo spostiamo a sinistra del cursore
        if (x + menuWidth > rect.width) x = x - menuWidth;
        // Se il menu esce in basso, lo spostiamo sopra il cursore
        if (y + menuHeight > rect.height) y = y - menuHeight;

        // Padding di sicurezza dai bordi
        x = Math.max(5, x);
        y = Math.max(5, y);

        setContextMenu({ x, y, item });
    };

    // 4. Azioni File System
    const handleRename = (item: FileItem) => {
        const newName = prompt(`Rinomina "${item.name}" in:`, item.name);
        if (newName && newName.trim() !== "" && newName !== item.name) {
            sendMessage({
                msgid: 75,
                id: item.id,
                type: item.type,
                new_name: newName.trim(),
                parent_id: currentFolderId
            });
        }
        setContextMenu(null);
    };

    const handleMove = (item: FileItem, targetFolderId: number) => {
        sendMessage({
            msgid: 77,
            id: item.id,
            type: item.type,
            new_parent_id: targetFolderId,
            current_parent_id: currentFolderId
        });
        setContextMenu(null);
    };

    const handleDelete = (item: FileItem) => {
        if (confirm(`Eliminare definitivamente "${item.name}"?`)) {
            sendMessage({
                msgid: 74,
                id: item.id,
                type: item.type,
                parent_id: currentFolderId
            });
        }
        setContextMenu(null);
    };

    const navigateTo = (folderId: number) => {
        setHistory([...history, currentFolderId]);
        setCurrentFolderId(folderId);
    };

    const goBack = () => {
        const prev = [...history];
        const last = prev.pop();
        setHistory(prev);
        setCurrentFolderId(last || 0);
    };

    return (
        <div
            ref={browserRef}
            className="flex flex-col w-full h-full bg-zinc-950 border-r border-zinc-800 font-sans select-none relative overflow-hidden"
        >
            {/* TOOLBAR */}
            <div className="flex items-center justify-between p-3 border-b border-zinc-800 bg-zinc-900/30 shrink-0">
                <span className="text-[10px] font-black text-zinc-500 uppercase tracking-widest">Library</span>
                <div className="flex gap-1">
                    <button
                        title="New Folder"
                        onClick={() => { const n = prompt("Nome cartella:"); if(n) sendMessage({msgid: 73, name: n, parent_id: currentFolderId}) }}
                        className="p-1 text-zinc-400 hover:text-purple-400 transition-colors"
                    >
                        <FolderPlus size={14} />
                    </button>
                    <button
                        title="New Shader"
                        onClick={() => { const n = prompt("Nome shader:"); if(n) sendMessage({msgid: 72, name: n, folder_id: currentFolderId, source_b64: btoa("")}) }}
                        className="p-1 text-zinc-400 hover:text-purple-400 transition-colors"
                    >
                        <FilePlus size={14} />
                    </button>
                </div>
            </div>

            {/* BREADCRUMB */}
            <div className="flex items-center gap-2 p-2 bg-black/20 text-[10px] border-b border-zinc-900 shrink-0">
                {currentFolderId !== 0 && (
                    <button onClick={goBack} className="text-purple-500 hover:text-purple-300 flex items-center gap-1 font-bold">
                        <ChevronLeft size={12} /> UP
                    </button>
                )}
                <span className="text-zinc-600 truncate uppercase tracking-tighter">
                    {currentFolderId === 0 ? "root" : `dir_${currentFolderId}`}
                </span>
            </div>

            {/* LISTA CONTENUTI */}
            <div className="flex-1 overflow-y-auto no-scrollbar p-2">
                <div className="space-y-0.5">
                    {items.map((item) => (
                        <div
                            key={`${item.type}-${item.id}`}
                            onClick={() => item.type === 'folder' && navigateTo(item.id)}
                            onContextMenu={(e) => handleContextMenu(e, item)}
                            className={`group flex items-center justify-between p-2 rounded transition-all border border-transparent 
                                ${item.type === 'folder' ? 'cursor-pointer hover:bg-zinc-900' : 'cursor-default hover:border-zinc-800/50'}`}
                        >
                            <div className="flex items-center gap-3 overflow-hidden">
                                {item.type === 'folder' ? (
                                    <Folder size={14} className="text-amber-500 fill-amber-500/20 shrink-0" />
                                ) : (
                                    /* ICONA SHADER MIGLIORATA */
                                    <div className="relative shrink-0">
                                        {/* Piccolo bagliore dietro l'icona */}
                                        <div className="absolute inset-0 bg-purple-500/20 blur-[4px] rounded-full" />
                                        <Cuboid
                                            size={14}
                                            className="text-purple-400 relative z-10 animate-pulse-slow"
                                            strokeWidth={2.5}
                                        />
                                    </div>
                                )}
                                <span className={`text-sm truncate ${item.type === 'folder' ? 'text-zinc-300 group-hover:text-white' : 'text-zinc-400 group-hover:text-purple-300'}`}>
                                    {item.name}
                                </span>
                            </div>
                        </div>
                    ))}
                    {items.length === 0 && (
                        <div className="py-10 text-center text-[10px] text-zinc-700 uppercase tracking-widest">Empty</div>
                    )}
                </div>
            </div>

            {/* --- CONTEXT MENU ABSOLUTE --- */}
            {contextMenu && (
                <div
                    className="absolute z-[100] w-52 bg-zinc-900 border border-zinc-800 rounded-lg shadow-2xl py-1 animate-in fade-in zoom-in-95 duration-100 shadow-black"
                    style={{ top: `${contextMenu.y}px`, left: `${contextMenu.x}px` }}
                    onClick={(e) => e.stopPropagation()} // Impedisce la chiusura cliccando dentro il menu
                >
                    <div className="px-3 py-1.5 text-[9px] font-black text-zinc-500 uppercase border-b border-zinc-800 mb-1 truncate bg-white/5">
                        {contextMenu.item.name}
                    </div>

                    {contextMenu.item.type === 'shader' && (
                        <button
                            onClick={() => { onSelectShader(contextMenu.item.id, contextMenu.item.name); setContextMenu(null); }}
                            className="w-full flex items-center gap-2 px-3 py-2 text-xs text-white bg-purple-600/10 hover:bg-purple-600 transition-colors"
                        >
                            <ExternalLink size={12} /> Open in Editor
                        </button>
                    )}

                    <button
                        onClick={() => handleRename(contextMenu.item)}
                        className="w-full flex items-center gap-2 px-3 py-2 text-xs text-zinc-300 hover:bg-zinc-800 transition-colors"
                    >
                        <Edit2 size={12} /> Rename
                    </button>

                    {/* SOTTOMENU MOVE TO */}
                    <div className="group/move relative">
                        <div className="w-full flex items-center justify-between px-3 py-2 text-xs text-zinc-300 hover:bg-zinc-800 cursor-default transition-colors">
                            <div className="flex items-center gap-2"><Move size={12} /> Move to...</div>
                            <ChevronLeft size={10} className="rotate-180 opacity-50" />
                        </div>

                        <div className={`hidden group-hover/move:block absolute top-0 w-48 bg-zinc-900 border border-zinc-800 rounded-lg shadow-2xl py-1 shadow-black
                            ${contextMenu.x + 400 > (browserRef.current?.clientWidth || 0) ? 'right-full mr-1' : 'left-full ml-1'}`}
                        >
                            <div className="px-3 py-1 text-[8px] font-bold text-zinc-500 uppercase border-b border-zinc-800/50 mb-1">Destination</div>

                            {currentFolderId !== 0 && (
                                <button
                                    onClick={() => handleMove(contextMenu.item, 0)}
                                    className="w-full text-left px-3 py-1.5 text-xs text-purple-400 hover:bg-zinc-800 transition-colors font-mono"
                                >
                                    / (root)
                                </button>
                            )}

                            {items.filter(i => i.type === 'folder' && i.id !== contextMenu.item.id).map(f => (
                                <button
                                    key={f.id}
                                    onClick={() => handleMove(contextMenu.item, f.id)}
                                    className="w-full text-left px-3 py-1.5 text-xs text-zinc-300 hover:bg-zinc-800 truncate transition-colors"
                                >
                                    {f.name}/
                                </button>
                            ))}
                        </div>
                    </div>

                    <div className="h-px bg-zinc-800 my-1" />

                    <button
                        onClick={() => handleDelete(contextMenu.item)}
                        className="w-full flex items-center gap-2 px-3 py-2 text-xs text-red-400 hover:bg-red-600 hover:text-white transition-colors"
                    >
                        <Trash2 size={12} /> Delete
                    </button>
                </div>
            )}
        </div>
    );
}