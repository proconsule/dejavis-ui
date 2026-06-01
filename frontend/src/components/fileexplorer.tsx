import {
  Folder, FileAudio, HardDrive, ArrowUpLeft, Play, ListPlus, FolderOpen
} from "lucide-react";
import {
  ContextMenu, ContextMenuContent, ContextMenuItem, ContextMenuTrigger,
} from "@/components/ui/context-menu";

import {buildFullPath, getRelPath} from "@/lib/dejavis_utils.ts";

import React, {useEffect, useState} from 'react';
import { ChevronRight, Home } from 'lucide-react';
import {useWS} from "@/WebSocketContext.tsx";

interface BreadcrumbPathProps {
  path: string;
  onNavigate: (fullPath: string) => void;
}



export const BreadcrumbPath = ({ path, onNavigate }: BreadcrumbPathProps) => {
    console.log(path)
    const segments = path?.split('/').filter(Boolean) || [];

  return (
      <nav className="flex items-center space-x-1 text-[10px] font-mono uppercase tracking-wider text-zinc-500">
        <button
            onClick={() => onNavigate("")}
            className="hover:text-green-500 transition-colors p-1"
        >
          <Home size={12} />
        </button>

        {segments.map((segment, index) => {
          const cumulativePath = segments.slice(0, index + 1).join('/');

          return (
              <React.Fragment key={cumulativePath}>
                <ChevronRight size={10} className="text-zinc-700 shrink-0" />
                <button
                    onClick={() => onNavigate(cumulativePath)}
                    className="hover:text-green-400 px-1.5 py-0.5 rounded transition-all whitespace-nowrap"
                >
                  {segment}
                </button>
              </React.Fragment>
          );
        })}
      </nav>
  );
};


interface FileExplorerProps {
    idx:number;
    type:number;
}

export function FileExplorer({ idx,type }: FileExplorerProps) {

    const { lastJsonMessage,sendMessage } = useWS();

    const [fileSystemData, setFileSystemData] = useState<any>(null);

    const onFileSelect=(relPath:string) => {
        const finalPath = buildFullPath(relPath,fileSystemData);
        if (finalPath) sendMessage({ msgid: 5004,idx: idx,type:type, path: finalPath });
    };

    const onNavigate= (path:string) => sendMessage({ msgid: 5003,idx: idx,type:type, path: path });
    const onQueueAdd = (path:string) => sendMessage({ msgid: 5020,idx: idx, path: path });

    useEffect(() => {
        if (lastJsonMessage?.msgid !== 5003) return;
        if (lastJsonMessage.idx === idx) {
            console.log(lastJsonMessage);
            setFileSystemData(lastJsonMessage);
        }
    }, [idx,lastJsonMessage]);


    if (!fileSystemData) {
    return (
        <div className="p-8 text-center text-xs text-slate-400 animate-pulse">
          In attesa di dati dal sistema...
        </div>
    );
  }



  return (
      <div className="bg-white rounded-xl border shadow-sm overflow-hidden flex flex-col h-[450px] select-none">
        <div className="bg-slate-50 border-b px-4 py-2 flex items-center gap-2 text-xs font-mono text-slate-500">
          <HardDrive className="h-3.5 w-3.5" />
          <span className="truncate opacity-50">{fileSystemData.absPath || fileSystemData.basePath}</span>
          <span className="truncate font-bold text-purple-700 italic">
            <BreadcrumbPath
                path={fileSystemData.relReq}
                onNavigate={onNavigate}
            />
        </span>
        </div>

        <div className="flex-1 overflow-y-auto">
          <table className="w-full text-left border-collapse">
            <tbody className="divide-y divide-slate-50">
            {fileSystemData.absPath !== "" && (
                <tr
                    onDoubleClick={() => onNavigate(getRelPath(fileSystemData) ?? "")}
                    className="hover:bg-slate-100/50 cursor-pointer group transition-colors"
                >
                  <td colSpan={2} className="px-4 py-2 flex items-center gap-3 text-slate-400 italic text-sm">
                    <ArrowUpLeft className="h-4 w-4 group-hover:-translate-y-0.5 group-hover:-translate-x-0.5 transition-transform" />
                    .. (Up Level)
                  </td>
                </tr>
            )}

            {/* Lista File e Cartelle */}
            {fileSystemData.entries?.map((entry:any, idx:any) => (
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

            {fileSystemData.entries?.length === 0 && (
                <tr>
                  <td colSpan={2} className="px-4 py-8 text-center text-sm text-slate-400">
                    Empty directory
                  </td>
                </tr>
            )}
            </tbody>
          </table>
        </div>
      </div>
  );
}