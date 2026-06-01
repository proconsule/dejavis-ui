export const formatTime = (s: number) => {
    if (!s || s < 0) return "00:00";
    const mins = Math.floor(s / 60);
    const secs = Math.floor(s % 60);
    return `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
};

export const buildFullPath = (relPath: string,fileSystemData:any) => {
    if (typeof relPath !== 'string') return null;

    // 1. Recuperiamo la root (assoluta o base)
    const root = fileSystemData?.absPath || fileSystemData?.basePath;
    if (!root) return null;

    // 2. Determiniamo il separatore corretto basandoci sulla root
    // Se la root contiene \, siamo su Windows. Altrimenti usiamo /
    const isWindows = root.includes('\\');
    const sep = isWindows ? '\\' : '/';

    // 3. Pulizia della Root: rimuoviamo eventuali slash/backslash finali
    const cleanRoot = root.replace(/[\\/]+$/, "");

    // 4. Pulizia e Normalizzazione del relPath:
    // Sostituiamo tutti i separatori errati con quello corretto del sistema
    // e rimuoviamo quelli in testa per evitare doppi separatori
    const normalizedRelPath = relPath
        .replace(/[\\/]+/g, sep)    // Sostituisce ogni / o \ con il sep corretto
        .replace(/^[\\/]+/, "");    // Rimuove sep iniziali

    // 5. Unione finale
    return `${cleanRoot}${sep}${normalizedRelPath}`;
};

export const getRelPath = (data:any) => {
    // 1. Recuperiamo il path (se è null/undefined usiamo stringa vuota)
    const current = data?.relReq || "";

    // 2. Se siamo già in root, non c'è un genitore
    if (current === "" || current === "/" || current === "\\") return "";

    // 3. Rimuoviamo l'eventuale slash finale per evitare errori di split
    const normalized = current.replace(/[\\/]$/, "");

    // 4. Dividiamo il path, togliamo l'ultimo elemento e riuniamo
    const parts = normalized.split(/[\\/]/);
    parts.pop();

    // 5. Riuniamo usando lo stesso separatore originale o quello che preferisci
    const sep = current.includes('\\') ? '\\' : '/';
    return parts.join(sep);
};
