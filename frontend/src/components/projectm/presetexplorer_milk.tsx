import {
    Music, Star, Trash2, Play, Hash, Info, AlertCircle, Tag, StarHalf, FolderPlusIcon
} from "lucide-react";
import {
    ContextMenu,
    ContextMenuContent,
    ContextMenuItem,
    ContextMenuTrigger,
    ContextMenuSeparator,
    ContextMenuShortcut,
    ContextMenuSub,
    ContextMenuSubContent,
    ContextMenuSubTrigger,
} from "@/components/ui/context-menu.tsx";

// Interfacce per i dati
export interface PlaylistMinimal {
    id: number;
    name: string;
}

export interface CategoryNode {
    id: number;
    name: string;
    parent_id: number | null;
    children: CategoryNode[];
}

export interface PresetEntry {
    id: number;
    name: string;
    category: number;
    rating: number;
    is_favorite: boolean;
    hash: string;
}

interface PresetExplorerProps {
    presets: PresetEntry[] | null | undefined;
    categories: CategoryNode[] | null;
    onMoveToCategory: (presetId: number, categoryId: number) => void;
    onPlay: (id: number) => void;
    onDelete: (id: number) => void;
    onToggleFavorite: (id: number, current_state: boolean) => void;
    onSetRating: (id: number, rating: number) => void;
    onShowInfo?: (id: number) => void;

}

export function PresetExplorer_Milk({
                                        presets,
                                        categories,
                                        onMoveToCategory,
                                        onPlay,
                                        onDelete,
                                        onToggleFavorite,
                                        onSetRating,
                                        onShowInfo
                                    }: PresetExplorerProps) {


    const getCategoryPath = (categoryId: number, nodes: CategoryNode[]): string => {
        const findPath = (currentNodes: CategoryNode[], targetId: number): string[] | null => {
            for (const node of currentNodes) {
                if (node.id === targetId) {
                    return [node.name];
                }
                if (node.children && node.children.length > 0) {
                    const childPath = findPath(node.children, targetId);
                    if (childPath) {
                        return [node.name, ...childPath];
                    }
                }
            }
            return null;
        };

        const pathArray = findPath(nodes, categoryId);

        return pathArray ? pathArray.join(" > ") : "Uncategorized";
    };

    const renderCategoryMenu = (nodes: CategoryNode[], presetId: number) => {
        return nodes.map(cat => (
            cat.children && cat.children.length > 0 ? (
                <ContextMenuSub key={cat.id}>
                    <ContextMenuSubTrigger className="text-xs">
                        <Tag className="mr-2 h-3 w-3 opacity-50" />
                        {cat.name}
                    </ContextMenuSubTrigger>
                    <ContextMenuSubContent>
                        {/* Opzione per selezionare la categoria padre stessa */}
                        <ContextMenuItem
                            className="text-xs font-bold"
                            onClick={() => onMoveToCategory(presetId, cat.id)}
                        >
                            Sposta in {cat.name}
                        </ContextMenuItem>
                        <ContextMenuSeparator />
                        {/* Chiamata ricorsiva passando lo stesso presetId */}
                        {renderCategoryMenu(cat.children, presetId)}
                    </ContextMenuSubContent>
                </ContextMenuSub>
            ) : (
                <ContextMenuItem
                    key={cat.id}
                    className="text-xs"
                    onClick={() => onMoveToCategory(presetId, cat.id)}
                >
                    <Tag className="mr-2 h-3 w-3 opacity-50" />
                    {cat.name}
                </ContextMenuItem>
            )
        ));
    };

    if (!presets) {
        return (
            <div className="bg-white rounded-xl border border-slate-200 shadow-sm flex items-center justify-center h-[500px]">
                <div className="flex flex-col items-center gap-2 text-slate-400">
                    <div className="animate-spin rounded-full h-6 w-6 border-b-2 border-purple-500" />
                    <span className="text-xs font-medium italic">Sincronizzazione database...</span>
                </div>
            </div>
        );
    }

    return (
        <div className="bg-white rounded-xl border border-slate-200 shadow-sm overflow-hidden flex flex-col h-[500px] select-none">

            <div className="bg-slate-50 border-b px-4 py-3 flex items-center justify-between">
                <div className="flex items-center gap-2 text-slate-700">
                    <Hash className="h-4 w-4 text-purple-500" />
                    <span className="text-xs font-bold uppercase tracking-widest font-mono">
                        Preset Database
                    </span>
                </div>
                <div className="text-[10px] font-bold px-2 py-0.5 bg-slate-200 text-slate-600 rounded uppercase">
                    {presets.length} Presets
                </div>
            </div>

            <div className="flex-1 overflow-y-auto custom-scrollbar">
                <table className="w-full text-left border-collapse">
                    <thead>
                    <tr className="text-[10px] uppercase text-slate-400 bg-slate-50/50 sticky top-0 z-10">
                        <th className="px-4 py-2 font-semibold">id</th>
                        <th className="px-4 py-2 font-semibold">Preset & Category</th>
                        <th className="px-4 py-2 font-semibold text-right">Rating</th>
                    </tr>
                    </thead>
                    <tbody className="divide-y divide-slate-50">
                    {presets.length > 0 ? (
                        presets.map((preset) => {
                            if (!preset || !preset.id) return null;

                            return (
                                <ContextMenu key={preset.id}>
                                    <ContextMenuTrigger asChild>
                                        <tr
                                            onDoubleClick={() => onPlay(preset.id)}
                                            className="group hover:bg-purple-50/40 cursor-pointer transition-all duration-150"
                                        >
                                            <td className="text-sm px-4 py-2.5 gap-0">{preset.id}</td>
                                            <td className="px-4 py-2.5">
                                                <div className="flex items-center gap-3">

                                                    <div className="relative flex-shrink-0">
                                                        <div className={`p-2 rounded-lg transition-colors ${preset.is_favorite ? 'bg-amber-50' : 'bg-slate-100'}`}>
                                                            <Music className={`h-4 w-4 ${preset.is_favorite ? 'text-amber-500' : 'text-slate-400'}`} />
                                                        </div>
                                                        {preset.is_favorite && (
                                                            <div className="absolute -top-1 -right-1">
                                                                <Star className="h-3 w-3 fill-amber-400 text-amber-400" />
                                                            </div>
                                                        )}
                                                    </div>

                                                    <div className="flex flex-col min-w-0">
                                                            <span className="text-sm font-semibold text-slate-700 truncate group-hover:text-purple-700">
                                                                {preset.name || "Senza Nome"}
                                                            </span>
                                                        <div className="flex items-center gap-2 mt-0.5">
                                                            <div className="flex items-center gap-1 text-[9px] font-bold text-purple-500 uppercase bg-purple-50 px-1.5 rounded-sm">
                                                                <Tag className="h-2 w-2" />
                                                                {getCategoryPath(Number(preset.category), categories || [])}
                                                            </div>
                                                            <span className="text-[9px] font-mono text-slate-400">
                                                                    {preset.hash ? preset.hash.substring(0, 8) : '--------'}
                                                                </span>
                                                        </div>
                                                    </div>
                                                </div>
                                            </td>

                                            <td className="px-4 py-2 text-right">
                                                <div className="flex justify-end gap-1">
                                                    {[...Array(5)].map((_, i) => (
                                                        <div
                                                            key={i}
                                                            className={`w-1.5 h-1.5 rounded-full transition-all ${
                                                                i < (preset.rating || 0) ? 'bg-amber-400 scale-110' : 'bg-slate-200'
                                                            }`}
                                                        />
                                                    ))}
                                                </div>
                                            </td>
                                        </tr>
                                    </ContextMenuTrigger>

                                    <ContextMenuContent className="w-60">
                                        <ContextMenuItem onClick={() => onPlay(preset.id)}>
                                            <Play className="mr-2 h-4 w-4 text-green-600" />
                                            Use Preset
                                            <ContextMenuShortcut>↵</ContextMenuShortcut>
                                        </ContextMenuItem>

                                        <ContextMenuItem onClick={() => onToggleFavorite(preset.id, preset.is_favorite)}>
                                            <Star className={`mr-2 h-4 w-4 ${preset.is_favorite ? 'fill-amber-400 text-amber-400' : ''}`} />
                                            {preset.is_favorite ? "Remove from favorites" : "Add to favorites"}
                                        </ContextMenuItem>

                                        <ContextMenuSeparator />

                                        <ContextMenuSub>
                                            <ContextMenuSubTrigger>
                                                <StarHalf className="mr-2 h-4 w-4 text-amber-500" />
                                                Rate Preset
                                            </ContextMenuSubTrigger>
                                            <ContextMenuSubContent className="w-36">
                                                {[1, 2, 3, 4, 5].map((star) => (
                                                    <ContextMenuItem
                                                        key={star}
                                                        onClick={() => onSetRating(preset.id, star)}
                                                        className="flex justify-between items-center"
                                                    >
                                                        <div className="flex gap-0.5">
                                                            {[...Array(star)].map((_, i) => (
                                                                <Star key={i} className="h-3 w-3 fill-amber-400 text-amber-400" />
                                                            ))}
                                                        </div>
                                                        <span className="text-[10px] font-mono text-slate-400">{star}</span>
                                                    </ContextMenuItem>
                                                ))}
                                                <ContextMenuSeparator />
                                                <ContextMenuItem onClick={() => onSetRating(preset.id, 0)} className="text-slate-400 text-[10px]">
                                                    Remove rating
                                                </ContextMenuItem>
                                            </ContextMenuSubContent>
                                        </ContextMenuSub>

                                        <ContextMenuSub>
                                            <ContextMenuSubTrigger>
                                                <FolderPlusIcon className="mr-2 h-4 w-4 text-amber-600" />
                                                Move in category
                                            </ContextMenuSubTrigger>
                                            <ContextMenuSubContent className="w-48">

                                                {categories && renderCategoryMenu(categories, preset.id)}
                                            </ContextMenuSubContent>
                                        </ContextMenuSub>

                                        {onShowInfo && (
                                            <ContextMenuItem onClick={() => onShowInfo(preset.id)}>
                                                <Info className="mr-2 h-4 w-4 text-blue-500" />
                                                Dettagli Tecnici
                                            </ContextMenuItem>
                                        )}

                                        <ContextMenuSeparator />

                                        <ContextMenuItem
                                            onClick={() => onDelete(preset.id)}
                                            className="text-red-600 focus:bg-red-50 focus:text-red-700 font-medium"
                                        >
                                            <Trash2 className="mr-2 h-4 w-4" />
                                            Remove from database
                                            <ContextMenuShortcut>⌫</ContextMenuShortcut>
                                        </ContextMenuItem>
                                    </ContextMenuContent>
                                </ContextMenu>
                            );
                        })
                    ) : (
                        <tr>
                            <td colSpan={2} className="px-4 py-16 text-center">
                                <div className="flex flex-col items-center gap-2 opacity-30 text-slate-500">
                                    <AlertCircle className="h-8 w-8" />
                                    <span className="text-sm italic font-medium">Database vuoto</span>
                                </div>
                            </td>
                        </tr>
                    )}
                    </tbody>
                </table>
            </div>

            <div className="bg-slate-50 border-t px-4 py-2 text-[10px] text-slate-400 flex justify-between font-mono">
                <span className="flex items-center gap-1.5 text-green-600 font-bold">
                    <div className="w-1.5 h-1.5 rounded-full bg-green-500 animate-pulse" />
                    SQLITE_DB_ACTIVE
                </span>
                <span>SYSTEM_SYNC_OK</span>
            </div>
        </div>
    );
}

export default PresetExplorer_Milk;