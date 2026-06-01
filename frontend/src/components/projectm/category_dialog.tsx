import { useState } from 'react';
import {
    Dialog,
    DialogContent,
    DialogHeader,
    DialogTitle,
    DialogFooter,
    DialogTrigger,
} from "@/components/ui/dialog";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { FolderPlus } from "lucide-react";
import { CategoryTree, type CategoryNode } from "./categorytreenode";

interface CreateCategoryDialogProps {
    categories: CategoryNode[];
    onAdd: (name: string, parentId: number | null) => void;
}

export function CreateCategoryDialog({ categories, onAdd }: CreateCategoryDialogProps) {
    const [open, setOpen] = useState(false);
    const [name, setName] = useState("");
    const [selectedParentId, setSelectedParentId] = useState<number | null>(null);

    const handleConfirm = () => {
        if (name.trim()) {
            onAdd(name.trim(), selectedParentId);
            setName("");
            setSelectedParentId(null);
            setOpen(false);
        }
    };

    return (
        <Dialog open={open} onOpenChange={setOpen}>
            <DialogTrigger asChild>
                <Button variant="ghost" size="icon" className="h-6 w-6 text-slate-400 hover:text-purple-600">
                    <FolderPlus size={14} />
                </Button>
            </DialogTrigger>
            <DialogContent className="sm:max-w-[425px]">
                <DialogHeader>
                    <DialogTitle className="flex items-center gap-2">
                        <FolderPlus className="h-5 w-5 text-purple-600" />
                        New Category
                    </DialogTitle>
                </DialogHeader>

                <div className="grid gap-4 py-4">
                    <div className="grid gap-2">
                        <Label htmlFor="name">Category Name</Label>
                        <Input
                            id="name"
                            value={name}
                            onChange={(e) => setName(e.target.value)}
                            placeholder="Es: Psychedelic, Minimal..."
                        />
                    </div>

                    <div className="grid gap-2">
                        <Label>Select Parent Category (root if not set)</Label>
                        <div className="border rounded-md p-2 max-h-[200px] overflow-y-auto bg-slate-50/50">
                            <button
                                onClick={() => setSelectedParentId(null)}
                                className={`w-full text-left px-2 py-1.5 rounded text-xs mb-1 flex items-center gap-2 ${
                                    selectedParentId === null ? "bg-purple-100 text-purple-700 font-bold" : "hover:bg-slate-200"
                                }`}
                            >
                                <div className="w-3 h-3 border border-dashed border-slate-400 rounded-sm" />
                                [ Root ]
                            </button>
                            <CategoryTree
                                categories={categories}
                                selectedId={selectedParentId}
                                onSelect={(id) => setSelectedParentId(id)}
                                onDelete={() => setSelectedParentId(null)}
                            />
                        </div>
                    </div>
                </div>

                <DialogFooter>
                    <Button variant="outline" onClick={() => setOpen(false)}>Cancel</Button>
                    <Button onClick={handleConfirm} disabled={!name.trim()} className="bg-purple-600 hover:bg-purple-700 text-white">
                        Create Category
                    </Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    );
}