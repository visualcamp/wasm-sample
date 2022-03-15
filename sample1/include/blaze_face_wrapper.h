#pragma once

#include <array>
#include <tuple>
#include <utility>
#include <vector>

#include "cutemodel/cute_model.h"
#include "opencv2/opencv.hpp"

namespace vc {
using Score = float;
using Angle = double;
using Point = cv::Point2f;
using Point3 = cv::Point3f;
using Ints = std::vector<int>;
using Floats = std::vector<float>;
using Doubles = std::vector<double>;

using Points = std::vector<Point>;
using Point3s = std::vector<Point3>;

using iROI = Ints;
using fROI = Floats;
using ROI = iROI;

using Landmarks = Points;
using Landmarks3D = Point3s;
using Image = cv::Mat;

using FBox = std::pair<fROI, Points>;
using Box = std::pair<ROI, Points>;
using Result = std::pair<ROI, Angle>;
using Detection = std::tuple<ROI, Score, Points>;

class BlazeFaceWrapper {
 public:
  BlazeFaceWrapper();
  Result Execute(const Image &input, Angle prior_rotation);

 protected:
  void BuildModel(const cute::CuteModelBuilder& builder);
  void InitAnchors();
  void InitOptions();

  Image PreProcess(const Image& image, Angle prior_rotation);
  Detection PostProcess(Angle rotation);

  Detection Run(const Image& image, Angle angle = 0);
  static cv::Mat NormalizeImage(const Image& image);
  Image ResizeImage(const Image& image);
  static Angle CalculateFaceAngleFromLandmarks(const Points& face_landmarks);
  static Image AlignImage(const Image& image, Angle angle, const std::vector<int>& dst_size, const ROI& roi={});

  FBox DecodeBox(const Floats& raw_box, const cv::Point2f& anchor) const;
  Box RealignOutputs(Floats roi, const Points& points, Angle rotation);


 private:
  int r_index = 0;
  int c_index = 0;
  double min_scale = 0.1484375;

  cute::CuteModel model;
  std::vector<int> target_size;
  std::vector<cv::Point2f> anchors;
  std::array<double, 2> rotation_anchor{};

  int num_strides = 0;
  int num_keypoints = 6;
  int keypoint_coord_offset = 4;

  int pad_left = 0;
  int pad_top = 0;

  double scale = 128.0;
  double threshold = 0.40;
  double resize_ratio = 0.0;
};

} // namespace vc