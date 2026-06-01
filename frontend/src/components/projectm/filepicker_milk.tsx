const FilePicker_Milk = ({ onUpload }: { onUpload: (name: string, data: string) => void }) => {
    const handleFileChange = (event: React.ChangeEvent<HTMLInputElement>) => {
        const file = event.target.files?.[0];
        if (!file) return;

        const reader = new FileReader();
        reader.onload = (e) => {
            const result = e.target?.result as string;
            const base64Data = result.split(',')[1];
            const fileName = file.name.replace('.milk', '');

            if (onUpload) {
                onUpload(fileName, base64Data);
            }
        };
        reader.readAsDataURL(file);
    };

    return (
        <div className="p-4">
            <label className="block mb-2 text-sm font-medium">Load Preset .milk</label>
            <input
                type="file"
                accept=".milk"
                onChange={handleFileChange}
                className="block w-full text-sm text-gray-500 file:mr-4 file:py-2 file:px-4 file:rounded-full file:border-0 file:text-sm file:font-semibold file:bg-blue-50 file:text-blue-700 hover:file:bg-blue-100"
            />
        </div>
    );
};

export default FilePicker_Milk;