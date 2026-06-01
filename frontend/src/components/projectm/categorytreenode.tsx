import {
    ChevronRight,
    Folder,
    FolderOpen,
    Trash2,
    Plus
} from "lucide-react";
import { useState } from "react";
import { cn } from "@/lib/utils"; // Assicurati di avere questa utility standard di shadcn

export interface CategoryNode {
    id: number;
    name: string;
    parent_id: number | null;
    children: CategoryNode[];
}

interface CategoryTreeProps {
    categories: CategoryNode[];
    selectedId: number | null;
    onSelect: (id: number | null) => void;
    onDelete: (id: number) => void;
    onAdd?: (parentId: number) => void;
}

export function CategoryTree({ categories, selectedId, onSelect, onDelete, onAdd }: CategoryTreeProps) {
    return (
        <div className="flex flex-col w-full select-none italic font-sans">
            {categories.map(node => (
                <CategoryTreeNode
                    key={node.id}
                    node={node}
                    selectedId={selectedId}
                    onSelect={onSelect}
                    onDelete={onDelete}
                    onAdd={onAdd}
                />
            ))}
        </div>
    );
}

interface CategoryTreeNodeProps {
    node: CategoryNode;
    selectedId: number | null;
    onSelect: (id: number | null) => void;
    onDelete: (id: number) => void;
    onAdd?: (parentId: number) => void;
    level?: number;
}

function CategoryTreeNode({ node, selectedId, onSelect, onDelete, onAdd, level = 0 }: CategoryTreeNodeProps) {
    const [isOpen, setIsOpen] = useState(true);
    const isSelected = selectedId === node.id;
    const hasChildren = node.children && node.children.length > 0;

    return (
        <div className="flex flex-col w-full">
            {/* Riga della categoria */}
            <div
                className={cn(
                    "group relative flex items-center h-8 w-full cursor-pointer transition-all duration-75 border-l-2",
                    isSelected
                        ? "bg-purple-50/80 border-purple-500 text-purple-900"
                        : "hover:bg-slate-100 border-transparent text-slate-600 hover:text-slate-900"
                )}
                style={{ paddingLeft: `${level * 16 + 4}px` }}
                onClick={() => onSelect(node.id)}
            >
                {/* Icona di espansione */}
                <div
                    className="w-5 h-5 flex items-center justify-center mr-0.5"
                    onClick={(e) => {
                        if (hasChildren) {
                            e.stopPropagation();
                            setIsOpen(!isOpen);
                        }
                    }}
                >
                    {hasChildren && (
                        <ChevronRight
                            size={14}
                            className={cn(
                                "transition-transform duration-200 text-slate-400",
                                isOpen && "rotate-90"
                            )}
                        />
                    )}
                </div>

                {/* Icona Cartella */}
                <div className="mr-2 text-slate-400">
                    {hasChildren ? (
                        isOpen ? <FolderOpen size={14} className="text-amber-500/80" /> : <Folder size={14} className="text-amber-500/80" />
                    ) : (
                        <Folder size={14} className="opacity-40" />
                    )}
                </div>

                {/* Nome Categoria */}
                <span className="text-[11px] font-medium truncate flex-1 tracking-tight">
                    {node.name}
                </span>

                {/* Toolbar Azioni (visibile solo in hover) */}
                <div className="hidden group-hover:flex items-center gap-0.5 pr-2 bg-gradient-to-l from-slate-100 via-slate-100 to-transparent pl-4">
                    {onAdd && (
                        <button
                            onClick={(e) => { e.stopPropagation(); onAdd(node.id); }}
                            className="p-1 hover:bg-slate-200 rounded text-slate-400 hover:text-blue-600 transition-colors"
                            title="Aggiungi sottocategoria"
                        >
                            <Plus size={12} />
                        </button>
                    )}
                    {node.id !== 1 && (
                        <button
                            onClick={(e) => { e.stopPropagation(); onDelete(node.id); }}
                            className="p-1 hover:bg-slate-200 rounded text-slate-400 hover:text-red-600 transition-colors"
                            title="Elimina"
                        >
                            <Trash2 size={12} />
                        </button>
                    )}
                </div>
            </div>

            {/* Container dei figli con linea guida verticale */}
            {hasChildren && isOpen && (
                <div
                    className="relative ml-[13px] border-l border-slate-200/60"
                >
                    {node.children.map((child: CategoryNode) => (
                        <CategoryTreeNode
                            key={child.id}
                            node={child}
                            selectedId={selectedId}
                            onSelect={onSelect}
                            onDelete={onDelete}
                            onAdd={onAdd}
                            level={0} // Il padding è gestito dal container relativo o dal calcolo del livello
                        />
                    ))}
                </div>
            )}
        </div>
    );
}