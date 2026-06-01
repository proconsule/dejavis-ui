import { useState } from 'react';
import axios from 'axios';

const FileUploaderWithProgress = () => {
    const [file, setFile] = useState(null);
    const [progress, setProgress] = useState(0);
    const [status, setStatus] = useState('');

    const handleFileChange = (e:any) => {
        setFile(e.target.files[0]);
        setProgress(0); // Reset progress quando cambia il file
    };

    const handleUpload = async () => {
        if (!file) {
            setStatus('Seleziona un file!');
            return;
        }

        const formData = new FormData();
        formData.append('file', file);

        try {
            setStatus('Caricamento...');
            //const response = await axios.post('/upload', formData, {
            const currentHost = window.location.hostname;
            const uploadUrl = `https://${currentHost}:8848/upload`;
            const response = await axios.post(uploadUrl, formData, {
            //const response = await axios.post('https://10.0.0.96:8848/upload', formData, {

                //const response = await axios.post('https://localhost:8848/upload', formData, {
                onUploadProgress: (progressEvent) => {
                    // Calcolo della percentuale
                    const percentCompleted = Math.round(
                        (progressEvent.loaded * 100) / (progressEvent?.total?? 1)
                    );
                    setProgress(percentCompleted);
                },
            });

            if (response.status === 200) {
                setStatus('Completato con successo!');
            }
        } catch (error) {
            console.error('Errore:', error);
            setStatus('Errore durante l\'upload.');
        }
    };

    return (
        <div style={{ padding: '20px', maxWidth: '400px' }}>
            <h3>Carica Video o Immagine</h3>
            <input type="file" onChange={handleFileChange} accept="image/*,video/*" />

            <button
                onClick={handleUpload}
                style={{ marginTop: '10px', display: 'block' }}
            >
                Avvia Caricamento
            </button>

            {/* Container della barra di avanzamento */}
            {progress > 0 && (
                <div style={{ marginTop: '20px' }}>
                    <div style={{
                        width: '100%',
                        backgroundColor: '#e0e0e0',
                        borderRadius: '10px',
                        overflow: 'hidden'
                    }}>
                        <div style={{
                            width: `${progress}%`,
                            height: '20px',
                            backgroundColor: progress === 100 ? '#4caf50' : '#2196f3',
                            transition: 'width 0.3s ease',
                            textAlign: 'center',
                            color: 'white',
                            fontSize: '12px',
                            lineHeight: '20px'
                        }}>
                            {progress}%
                        </div>
                    </div>
                </div>
            )}

            <p style={{ fontSize: '14px', color: '#666' }}>{status}</p>
        </div>
    );
};

export default FileUploaderWithProgress;