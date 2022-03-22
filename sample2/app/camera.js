import {ImageCapture} from './polyfil/imagecapture';

const delay = (ms, cb) =>
    new Promise((resolve) =>
        setTimeout(() => {
          if (cb) cb();
          resolve();
        }, ms),
    );

export class CameraThread{
  constructor() {
    const stream = await navigator.mediaDevices.getUserMedia({'video': true});
    const fps = 30;
    this.min_interval_ms = 1000 / fps;
    this.last_frame_time = Date.now();
    this.flag = false;
    this.running = false;
    this.track = null;
    this.callback = () => {};
  }
  
  init(stream) {
    const stream = await navigator.mediaDevices.getUserMedia({'video': true});
    this.releaseStreamTrack_();
    const tracks = stream.getVideoTracks();
    if (tracks) {
      this.track = tracks[0];
      this.imageCapture = new ImageCapture(this.track);
      return true;
    }
    return false;
  }

  release() {
    if (this.track) {
      this.stop();
      this.track.stop();
      this.track = null;
    }
  }

  start() {
    if (!this.running && this.checkStreamTrack_(this.track)) {
      this.running = true;
      this.processFunc_();
    }
  }

  stop() {
    this.running = false;
  }

  setCallback(callback) {
    this.callback = callback;
  }

  /** @private */
  async processFunc_() {
    while (this.running) {
      try {
        const bitmap = await imageCapture.grabFrame();
        this.callback(bitmap);
      } catch (e) {
        console.log(e);
      }
      const current_frame_time = Date.now();
      const current_delay_time = this.min_interval_ms - (current_frame_time - this.last_frame_time);
      this.last_frame_time = current_frame_time;
      await delay(current_delay_time);
    }
  }
  
  /** @private */
  checkStreamTrack_(track) {
    if (track === null || track.readyState !== 'live' || !track.enabled || track.muted) {
      if (track === null) {
        console.log('error checkStreamTrack_ ');
      } else {
        console.log(`error 
                    ready ${track.readyState !== 'live'}, 
                    enabled ${!track.enabled}, 
                    muted ${track.muted}`);
      }
      return false;
    }
    return true;
  }
}