"use client";

import {useEffect, useMemo, useState} from 'react';
import { useWS } from '../WebSocketContext';
import {
    SkipForward, SkipBack, Folder
} from 'lucide-react';
import { Button } from "@/components/ui/button";
import  FilePicker_Milk from "@/components/projectm/filepicker_milk.tsx";
import { PresetExplorer_Milk} from "@/components/projectm/presetexplorer_milk.tsx";
import {CategoryTree} from "@/components/projectm/categorytreenode.tsx";
import {CreateCategoryDialog} from "@/components/projectm/category_dialog.tsx";
import {Marquee} from "@/components/ui/marquee.tsx";


export interface CategoryNode {
    id: number;
    name: string;
    parent_id: number | null;
    children: CategoryNode[];
}

export function ProjectMDashboard({milkdbdata}:any) {
    const { lastJsonMessage, sendMessage } = useWS();

    useEffect(() => {
        console.log(milkdbdata);
    }, [milkdbdata]);

    useEffect(() => {
        if(lastJsonMessage.msgid == 4019){
            setcategories(lastJsonMessage.categories);
            console.log(lastJsonMessage);
        }
    }, [lastJsonMessage]);

    useEffect(() => {
        // Definiamo il comando da inviare al backend C++
        const catmsg = {
            msgid: 4019
        };

        // Inviamo il messaggio
        sendMessage(catmsg);

        // Opzionale: Log di debug
        console.log("📡 Richiesta inizializzazione inviata al WS");

    }, [sendMessage]);
/*
    const pm = lastJsonMessage?.visualizer?.projectm || {
        current_preset: "Nessun Preset Attivo",
        fps: 0,
        settings: {
            mesh_size: 32,
            fps_limit: 60,
            soft_cut_duration: 5,
            beat_sensitivity: 1.0,
            start_clean: false
        },
        browser: {
            basePath: "/presets",
            relReq: "",
            entries: []
        }
    };
*/

    const pm = lastJsonMessage?.projectm || {
        current_preset: "Nessun Preset Attivo",
        fps: 0,
        settings: {
    //        mesh_size: 32,
            fps_limit: 60,
            soft_cut_duration: 5,
            beat_sensitivity: 1.0,
    //        start_clean: false
        }
    };

    console.log(lastJsonMessage);
    const sendPMCommand = (msgid: number, command: string, value: any) => {
        sendMessage({ msgid, command, value });
    };



    const handleOnUpload = (name: string,contents:string) =>{
        sendMessage({msgid: 4021, name: name,b64_content:contents});
    }

    const handleSetRating = (_presetid: number,_star:number) =>{
        sendMessage({msgid: 4023, presetid: _presetid,star:_star});
    }

    const handleToggleFavorite = (_presetid: number,currval:boolean) =>{
        console.log(currval);
        sendMessage({msgid: 4024, presetid: _presetid,favorite: currval ? 0: 1});
    }

    const handleDeleteCategory = (id: number) => {

        if (id === 1) return;

        if (window.confirm("Are you sure to delete this category? all presets will be moved in 'Uncategorized'.")) {
            sendMessage({
                msgid: 4017,
                category_id: id
            });
        }
    };

    const handleAddCategory = (name: string, parentId: number | null) => {

        sendMessage({
            msgid: 4018,
            name: name,
            parent_id: parentId ?? -1
        });

    };


    const handleonPlay = (_presetid: number) => {

        sendMessage({ msgid: 4026, presetid: _presetid });

    };

    const handledeletePreset = (_presetid: number) => {

        //sendMessage({ msgid: 4026, presetid: _presetid });

    };

    const handleMoveToCategory = (presetId: number, categoryId: number) => {
        // Invio al backend tramite WebSocket
        sendMessage({
            msgid: 4016,       // ID comando per lo spostamento
            preset_id: presetId,
            category_id: categoryId
        });
    };


    const [selectedCategoryId, setSelectedCategoryId] = useState<number | null>(null);

    const [categories, setcategories] = useState<any>(null);

    const getAllChildIds = (nodes: CategoryNode[], targetId: number): number[] => {
        const ids: number[] = [];

        const findAndCollect = (currentNodes: CategoryNode[], found: boolean) => {
            for (const node of currentNodes) {
                // Se abbiamo trovato la categoria target o siamo già dentro un suo ramo
                if (found || node.id === targetId) {
                    ids.push(node.id);
                    if (node.children) {
                        findAndCollect(node.children, true);
                    }
                } else if (node.children) {
                    findAndCollect(node.children, false);
                }
            }
        };

        findAndCollect(nodes, false);
        return ids;
    };

    const filteredPresets = useMemo(() => {
        if (!selectedCategoryId || !milkdbdata.presets) {
            return milkdbdata.presets;
        }

        // 1. Otteniamo tutti gli ID del ramo (target + figli)
        const allRelevantIds = getAllChildIds(categories || [], selectedCategoryId);

        // 2. Filtriamo i preset: il preset è incluso se la sua categoria è negli ID raccolti
        return milkdbdata.presets.filter((p: any) =>
            allRelevantIds.includes(Number(p.category))
        );
    }, [selectedCategoryId, milkdbdata.presets, categories]);

    return (
        <div className="flex flex-col gap-6 p-6 max-w-[1600px] mx-auto animate-in fade-in duration-500">



            <div className="flex items-center justify-between bg-slate-950/60 p-5 rounded-2xl border border-white/10 backdrop-blur-xl shadow-2xl w-full">
                {/* La colonna ora deve prendersi tutto lo spazio (flex-1) e non collassare (min-w-0) */}
                <div className="flex flex-col gap-1 flex-1 min-w-0">
                    <div className="flex items-center gap-2">
                        <div className="h-2 w-2 rounded-full bg-indigo-500 animate-pulse" />
                        <span className="text-[10px] font-black text-indigo-400 uppercase tracking-[0.2em]">
                projectM Video Layer 0
            </span>
                    </div>

                    {/* Questo div ora occupa il 100% della colonna */}
                    <div className="w-full min-w-0">
                        <h2 className="text-2xl font-black text-white drop-shadow-md truncate">
                            <Marquee >
                    <span className="pr-10">
                        {pm.current_preset.replace('.milk', '')}
                    </span>
                            </Marquee>
                        </h2>
                    </div>
                </div>

                {/* Se hai pulsanti o controlli a destra, finiranno qui grazie a justify-between */}
                <div className="flex items-center pl-4">
                    <div className="grid grid-cols-3 gap-1">
                        <Button
                            size="sm"
                            className="h-7 w-8 bg-white/5 hover:bg-white/10 border-white/5 p-0"
                            variant="outline"
                            onClick={() => sendPMCommand(4028, "prev", true)}
                        >
                            <SkipBack className="h-3 w-3" />
                        </Button>

                        <Button
                            size="sm"
                            className="h-7 flex-1 bg-indigo-600/80 hover:bg-indigo-500 text-[10px] font-bold px-2"
                            onClick={() => sendPMCommand(4027, "random", true)}
                        >
                            Random
                        </Button>

                        <Button
                            size="sm"
                            className="h-7 w-8 bg-white/5 hover:bg-white/10 border-white/5 p-0"
                            variant="outline"
                            onClick={() => sendPMCommand(4029, "next", true)}
                        >
                            <SkipForward className="h-3 w-3" />
                        </Button>
                    </div>
                </div>
            </div>


            <FilePicker_Milk
                onUpload={handleOnUpload}
            />

            <div className="flex w-full max-w-6xl mx-auto shadow-2xl rounded-xl overflow-hidden border border-slate-300 h-[600px] bg-white">


                <div className="w-64 shrink-0 bg-slate-50 border-r flex flex-col overflow-hidden">
                    <div className="flex-1 overflow-y-auto custom-scrollbar">
                        <div className="px-4 py-2 mt-4">
                            <div className="flex items-center justify-between mb-2">
                                <h3 className="text-[10px] font-black uppercase tracking-tighter text-slate-400">
                                    Categories
                                </h3>
                                {/* IL DIALOG DI AGGIUNTA */}
                                <CreateCategoryDialog
                                    categories={categories}
                                    onAdd={handleAddCategory}
                                />
                            </div>
                            <button
                                onClick={() => setSelectedCategoryId(null)}
                                className={`w-full flex items-center gap-2 px-2 py-1.5 rounded-md text-xs mb-2 ${
                                    selectedCategoryId === null ? "bg-slate-200 font-bold" : "text-slate-500"
                                }`}
                            >
                                <Folder size={14} /> ALL
                            </button>

                            <CategoryTree
                                categories={categories || []}
                                selectedId={selectedCategoryId}
                                onSelect={setSelectedCategoryId}
                                onDelete={handleDeleteCategory}
                            />
                        </div>


                    </div>
                </div>
                <div className="flex-1 bg-white">
                    <PresetExplorer_Milk
                        presets={filteredPresets}
                        categories={categories}
                        onSetRating={handleSetRating}
                        onToggleFavorite={handleToggleFavorite}
                        onPlay={handleonPlay}
                        onDelete={handledeletePreset}
                        onMoveToCategory={handleMoveToCategory}
                    />
                </div>
            </div>
        </div>

    );
}