import { ListMusic, Plus} from "lucide-react";
import { useState } from 'react';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogFooter, DialogTrigger} from "@/components/ui/dialog";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";

interface CreatePlaylistDialogProps {
    onCreate: (name: string) => void;
}

export function CreatePlaylistDialog({ onCreate }: CreatePlaylistDialogProps) {
    const [name, setName] = useState("");
    const [open, setOpen] = useState(false);

    const handleSubmit = () => {
        if (name.trim()) {
            onCreate(name.trim());
            setName("");
            setOpen(false);
        }
    };

    return (
        <Dialog open={open} onOpenChange={setOpen}>
            <DialogTrigger asChild>
                <button className="p-1 hover:bg-slate-100 rounded-full transition-colors text-purple-600">
                    <Plus className="h-4 w-4" />
                </button>
            </DialogTrigger>
            <DialogContent className="sm:max-w-[425px]">
                <DialogHeader>
                    <DialogTitle className="flex items-center gap-2">
                        <ListMusic className="h-5 w-5 text-purple-500" />
                        Nuova Playlist
                    </DialogTitle>
                </DialogHeader>
                <div className="grid gap-4 py-4">
                    <div className="space-y-2">
                        <label htmlFor="name" className="text-sm font-medium text-slate-500">
                            Nome della playlist
                        </label>
                        <Input
                            id="name"
                            placeholder="Es: Visuals Relax, Techno Party..."
                            value={name}
                            onChange={(e) => setName(e.target.value)}
                            onKeyDown={(e) => e.key === 'Enter' && handleSubmit()}
                            autoFocus
                        />
                    </div>
                </div>
                <DialogFooter>
                    <Button variant="outline" onClick={() => setOpen(false)}>Annulla</Button>
                    <Button
                        className="bg-purple-600 hover:bg-purple-700"
                        onClick={handleSubmit}
                        disabled={!name.trim()}
                    >
                        Crea Playlist
                    </Button>
                </DialogFooter>
            </DialogContent>
        </Dialog>
    );
}