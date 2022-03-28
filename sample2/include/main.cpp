#include <emscripten.h>

#include "opencv2/opencv.hpp"
#include "blaze_face_wrapper.h"
#include "cutemodel/cute_model.h"

typedef void (*face_callback) (int, int, int, int, int);
face_callback callback = nullptr;
vc::BlazeFaceWrapper face_wrapper;

extern "C" {
  EMSCRIPTEN_KEEPALIVE
  int findFace(char* buffer, int width, int height, int prior_angle_degree) {
    cv::Mat image_rgba(height, width, CV_8UC4, buffer);
    cv::Mat image_rgb;
    cv::cvtColor(image_rgba, image_rgb, cv::COLOR_RGBA2RGB);
    
    auto [roi, angle] = face_wrapper.Execute(image_rgb, prior_angle_degree * 3.141592 / 180);
    if (callback != nullptr) callback(roi[0], roi[1], roi[2], roi[3], static_cast<int>(angle * 180 / 3.141592));
    return static_cast<int>(angle * 180 / 3.141592);
  }
  
  EMSCRIPTEN_KEEPALIVE
  bool setFaceCallback(face_callback callback_) {
    callback = callback_;
    return true;
  }
}