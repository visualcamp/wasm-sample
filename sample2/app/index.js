import {CameraThread} from './camera.js';
import {WasmWrapper} from './wasm/wasm.js';

let cameraThread = null;
let wasmWrapper = new WasmWrapper();
const block = document.querySelector("div.block");
const video = document.querySelector('video');
const face = document.querySelector(".content .overlay");

const camWidth = 1280;
const camHeight = 720;
const canvasWidthPercent = 90;
block.style.width = canvasWidthPercent + "%";

export function startCamera() {
    navigator.mediaDevices.getUserMedia({video: { width: camWidth, height: camHeight }})
    .then(stream => {
        video.style.height = canvasWidthPercent * camHeight / camWidth + "vw";
        const track = stream.getVideoTracks()[0];
        cameraThread = new CameraThread();
        if (cameraThread.init(track)) {
            wasmWrapper.setFaceCallback(drawFace);
            video.srcObject = stream;
            cameraThread.start();
            cameraThread.setCallback((bitmap) => {
                wasmWrapper.processFaceDetection(bitmap);
            })
        } else {
            cameraThread = null;
        }
    })
}

export function stopCamera() {
    cameraThread.release();
}

/** @private */
function drawFace(left, top, right, bottom, angle) {
    const left_ = left / camWidth * 100;
    const top_ = top / camHeight * 100;
    const width_ = (right - left) / camWidth * 100;
    const height_ = (bottom - top) / camHeight * 100;
    const rotate = "rotate(" + angle + "deg)";
    
    face.style.left = left_ + "%";
    face.style.top = top_ + "%";
    face.style.width = width_ + "%";
    face.style.height = height_ + "%";
    face.style.transform = rotate;
}