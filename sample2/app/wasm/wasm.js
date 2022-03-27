export class WasmWrapper {
    constructor() {
        this.loadModuleScript_("./wasm/simd/WasmSample.js").then(() => {
            createModule().then(instance => {
                this.wasmModule = instance;
            });
        });
        this.angle = 0;
    }
    
    processFaceDetection(bitmap) {
        console.log("processFaceDetection");
        const time1 = Date.now();
        const buffer = this.createBuffer_(bitmap);
        const time2 = Date.now();
        const blob = this.convertBitmapToBlob_(bitmap);
        const time3 = Date.now();
        this.wasmModule.HEAPU8.set(blob.data, buffer);
        const time4 = Date.now();
        this.angle = this.wasmModule.ccall('findFace', 'number', ['number', 'number', 'number', 'number'], [buffer, bitmap.width, bitmap.height, this.angle]);
        const time5 = Date.now();
        this.freeBuffer_(buffer);
        const time6 = Date.now();
        console.log("Process time : ", (time2 - time1), (time3 - time2), (time4 - time3), (time5 - time4), (time6 - time5));
    }

    /** @private */
    loadModuleScript_(jsUrl) {
        return new Promise((resolve, reject) => {
        let script = document.createElement('script');
        script.onload = (() => {
            resolve();
        });
        script.onerror = (() => {
            reject();
        });
        script.async = true;
        script.src = jsUrl;
        script.crossOrigin = 'anonymous';
        document.getElementsByTagName('script')[0].parentNode.appendChild(script);
        });
    }

    setFaceCallback(callback) {
        let faceCallback = this.wasmModule.addFunction(callback, 'viiiii');
        this.wasmModule.ccall('setFaceCallback', 'boolean', ['number'], [faceCallback]);
    }    

    /** @private */
    createBuffer_(bitmap) {
        return this.wasmModule._malloc(bitmap.width * bitmap.height * 4);
    }

    /** @private */
    freeBuffer_(buffer) {
        this.wasmModule._free(buffer);
    }


    /** @private */
    convertBitmapToBlob_(bitmap) {
        if (!this.canvas) {
        this.canvas = document.createElement('canvas');
        }
        if (!this.preview) {
        this.preview = document.getElementById('preview');
        }

        this.canvas.width = bitmap.width;
        this.canvas.height = bitmap.height;

        const ctx = this.canvas.getContext('2d');
        ctx.drawImage(bitmap, 0, 0);
        if (this.isShowPreview) {
        this.preview.width = bitmap.width / 2;
        this.preview.height = bitmap.height / 2;
        const previewCtx = this.preview.getContext("2d");
        previewCtx.scale(0.5, 0.5);
        previewCtx.drawImage(this.canvas, 0, 0);
        }
        return ctx.getImageData(0, 0, this.canvas.width, this.canvas.height);
    }
}