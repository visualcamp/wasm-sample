#include <chrono>
#include <iostream>
#include <emscripten.h>

#include "cutemodel/cute_model.h"
#include "blaze_face_wrapper.h"
#include "vccc/log.hpp"
#include "sample_jpg.h"

int main() {
  vc::BlazeFaceWrapper face_wrapper;
  std::vector<unsigned char> sample_image(elon_jpg, elon_jpg + elon_jpg_len);
  auto image = cv::imdecode(sample_image, cv::IMREAD_COLOR);
  cv::cvtColor(image, image, cv::COLOR_BGR2RGB);

  using namespace std::chrono;
  high_resolution_clock::duration time_duration(0);
  for (auto i = 0 ; i < 100 ; i ++) {
    auto start_time = high_resolution_clock::now();
    auto [roi, angle] = face_wrapper.Execute(image, 0);
    time_duration += duration_cast<nanoseconds>(high_resolution_clock::now() - start_time);
  }
  LOGD("Avg time : ", time_duration.count() / (100 * 1000000.0));

  return 0;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void initEyeTracker() {
  
}

// EMSCRIPTEN_KEEPALIVE
// bool setJSCallbacks(long eye_tracker_ptr,
//                     uint16_t gazeCallback,
//                     uint16_t calibNextPointCallback,
//                     uint16_t calibProgressCallback,
//                     uint16_t calibFinishCallback,
//                     uint16_t statusCallback,
//                     uint16_t faceCallback) {
//   auto cb = std::make_unique<seeso::JSCallback>();
//   seeso::LinkData link_data{};
//   link_data.javascript.gaze = gazeCallback;
//   link_data.javascript.calibration_progress = calibNextPointCallback;
//   link_data.javascript.calibration_next_point = calibProgressCallback;
//   link_data.javascript.calibration_finish = calibFinishCallback;
//   link_data.javascript.status = statusCallback;
//   link_data.javascript.face = faceCallback;

//   cb->linkFunction(link_data);
//   if (eye_tracker_ptr == 0)
//     return false;
//   CAST_WRAPPER(eye_tracker_ptr)->setCallbackInterface(std::move(cb));
//   return true;
// }

// EMSCRIPTEN_KEEPALIVE
// bool addFrame(long eye_tracker_ptr, uint8_t* buffer, int width, int height, int colorFormat) {
//   static auto startTimestamp = getCurrentTime<std::chrono::milliseconds>();
//   auto curTime = getCurrentTime<std::chrono::milliseconds>();
//   int64_t timestamp = curTime - startTimestamp;
//   cv::Mat frame(height, width, CV_8UC4, buffer);
//   cv::cvtColor(frame, frame, cv::COLOR_RGBA2RGB);
//   return CAST_WRAPPER(eye_tracker_ptr)->addFrame(timestamp, frame.data, frame.cols, frame.rows);
// }

// EMSCRIPTEN_KEEPALIVE
// uintptr_t create_image_ptr(int width, int height) {
//   auto ptr = malloc(width * height * 4 * sizeof(uint8_t));
//   return (uintptr_t) ptr;
// }

// EMSCRIPTEN_KEEPALIVE
// uintptr_t create_ptr(int size) {
//   auto ptr = malloc(size * sizeof(uint8_t));
//   return (uintptr_t) ptr;
// }

// EMSCRIPTEN_KEEPALIVE
// void destroy_ptr(uint8_t* p) {
//   free(p);
// }
}