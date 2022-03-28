import { simd, threads } from "https://unpkg.com/wasm-feature-detect?module";

export class WasmWrapper {
    constructor() {
        this.loaded = false;
        this.angle = 0;
        this.checkFeatures_().then(({useSimd, useThread}) => {
            if (!useThread) {
                console.warn("Threads disabled, seems that the security requirements for SharedArrayBuffer are not met")
                return;
            }
            let dir = useSimd? "simd" : "nonsimd";
            this.loadModuleScript_("./wasm/" + dir + "/WasmSample.js").then(() => {
                createModule().then(instance => {
                    this.wasmModule = instance;
                    this.loaded = true;
                });
            });
        })
    }

    setFaceCallback(callback) {
        let faceCallback = this.wasmModule.addFunction(callback, 'viiiii');
        this.wasmModule.ccall('setFaceCallback', 'boolean', ['number'], [faceCallback]);
    }    
    
    processFaceDetection(bitmap) {
        const blob = this.convertBitmapToBlob_(bitmap);
        const buffer = this.createBuffer_(bitmap);
        this.wasmModule.HEAPU8.set(blob.data, buffer);
        this.angle = this.wasmModule.ccall(
            'findFace', 
            'number', 
            ['number', 'number', 'number', 'number'], 
            [buffer, bitmap.width, bitmap.height, this.angle]);
        this.freeBuffer_(buffer);
    }

    /** @private */
    async checkFeatures_() {
        let useSimd = await simd();
        let useThread = await threads();
        console.log(useSimd, useThread);
        return {useSimd, useThread};
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
            script.src = jsUrl;
            document.body.appendChild(script);
        });
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
        this.canvas.width = bitmap.width;
        this.canvas.height = bitmap.height;

        const ctx = this.canvas.getContext('2d');
        ctx.drawImage(bitmap, 0, 0);
        return ctx.getImageData(0, 0, this.canvas.width, this.canvas.height);
    }
}