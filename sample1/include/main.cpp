#include <chrono>
#include <emscripten.h>

#include "cutemodel/cute_model.h"
#include "blaze_face_wrapper.h"
#include "sample_jpg.h"

EMSCRIPTEN_KEEPALIVE
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
  printf("Avg time : %f\n", time_duration.count() / (100 * 1000000.0));

  return 0;
}
