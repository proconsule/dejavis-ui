import { Library, ListMusic, FolderPlusIcon, Play, Trash2 } from "lucide-react";
import {CreatePlaylistDialog} from "@/components/projectm/createplaylistdialog_milk.tsx";

import {
    ContextMenu,
    ContextMenuContent,
    ContextMenuItem,
    ContextMenuTrigger,
    ContextMenuSeparator,
    ContextMenuShortcut
} from "@/components/ui/context-menu";

interface PlaylistMinimal_Milk {
    id: number;
    name: string;
}

interface PlaylistSidebarProps {
    playlists: PlaylistMinimal_Milk[] | null | undefined;
    selectedId: number | null;
    onLoadPlaylist: (id: number | null) => void;
    onCreateNew: (_playlistname: string) => void;
    onDelete: (id: number) => void;
}

export function PlaylistExplorer_Milk({ playlists, selectedId, onLoadPlaylist, onCreateNew, onDelete }: PlaylistSidebarProps) {

    return (
        <div className="w-64 bg-slate-50 border-r border-slate-200 flex flex-col h-[500px] select-none">
            {/* Header Sidebar */}
            <div className="p-4 border-b bg-white flex items-center justify-between">
                <div className="flex items-center gap-2 font-bold text-slate-700 text-xs uppercase tracking-wider">
                    <Library className="h-4 w-4 text-purple-600" />
                    Playlist
                </div>
                <CreatePlaylistDialog onCreate={onCreateNew} />
            </div>

            <div className="flex-1 overflow-y-auto p-2 space-y-0.5 custom-scrollbar">
                {/* Opzione "Tutti i Preset" */}
                <button
                    onClick={() => onLoadPlaylist(null)}
                    className={`w-full flex items-center gap-2 px-3 py-2 rounded-lg text-sm font-medium transition-colors ${
                        selectedId === null
                            ? "bg-purple-100 text-purple-700 shadow-sm border border-purple-200/50"
                            : "text-slate-600 hover:bg-slate-200/50 border border-transparent"
                    }`}
                >
                    <FolderPlusIcon className="h-4 w-4" />
                    Tutti i Preset
                </button>

                <div className="my-2 border-t border-slate-200/60" />

                {playlists && playlists.length > 0 ? (
                    playlists.map((pl) => (
                        <ContextMenu key={pl.id}>
                            <ContextMenuTrigger>
                                <button
                                    onClick={() => onLoadPlaylist(pl.id)}
                                    className={`w-full flex items-center gap-2 px-3 py-2 rounded-lg text-sm transition-all ${
                                        selectedId === pl.id
                                            ? "bg-white shadow-sm border border-slate-200 text-purple-700 font-semibold"
                                            : "text-slate-600 hover:bg-slate-200/50 border border-transparent"
                                    }`}
                                >
                                    <ListMusic className={`h-4 w-4 ${selectedId === pl.id ? "text-purple-500" : "text-slate-400"}`} />
                                    <span className="truncate">{pl.name}</span>
                                </button>
                            </ContextMenuTrigger>

                            <ContextMenuContent className="w-48">
                                <ContextMenuItem onClick={() => onLoadPlaylist(pl.id)}>
                                    <Play className="mr-2 h-4 w-4 text-green-600" />
                                    Carica Playlist
                                    <ContextMenuShortcut>↵</ContextMenuShortcut>
                                </ContextMenuItem>

                                <ContextMenuSeparator />

                                <ContextMenuItem
                                    onClick={() => onDelete(pl.id)}
                                    className="text-red-600 focus:bg-red-50 focus:text-red-700"
                                >
                                    <Trash2 className="mr-2 h-4 w-4" />
                                    Elimina Playlist
                                    <ContextMenuShortcut>⌫</ContextMenuShortcut>
                                </ContextMenuItem>
                            </ContextMenuContent>
                        </ContextMenu>
                    ))
                ) : (
                    <div className="px-3 py-8 text-center text-[11px] text-slate-400 italic">
                        Nessuna playlist presente
                    </div>
                )}
            </div>

            <div className="p-2 border-t bg-slate-100/50">
                <div className="text-[9px] text-slate-400 font-mono text-center uppercase tracking-tighter">
                    Milkdrop Library Manager
                </div>
            </div>
        </div>
    );
}