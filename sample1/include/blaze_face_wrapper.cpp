#include "blaze_face_wrapper.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "opencv2/opencv.hpp"

#include "model/model_reader.h"
#include "vccc/log.hpp"
#include "vccc/math.hpp"

namespace vc {

BlazeFaceWrapper::BlazeFaceWrapper() {
  auto model_data = vc::ModelReader::ReadBlazeFaceModel();
  cute::CuteModelBuilder builder({{model_data.byte, model_data.size, 2, false}});
  BuildModel(builder);
};

//
// Module API
//
Result BlazeFaceWrapper::Execute(const Image &input, Angle prior_angle) {
  // Return VoidOutput if no image is passed
  if (input.empty()) {
    return {ROI(), 0};
  }

  auto [face_roi, face_score, face_landmarks] = Run(input, prior_angle);
  if (face_roi.empty()) {
    return {ROI(), 0};
  }

  auto rotation_result = CalculateFaceAngleFromLandmarks(face_landmarks);
  return {face_roi, rotation_result};
}

//
// Model
//
void BlazeFaceWrapper::BuildModel(const cute::CuteModelBuilder& builder) {
  builder.build(model);

  LOGD(">>> Init blaze-face: \n", model.summarize());

  auto dims = model.inputTensorDims(0);
  target_size = std::vector{dims[1], dims[2]};

  for (int i = 0; i < model.outputTensorCount(); ++i) {
    const auto& tensor = model.outputTensor(i);
    if (cute::tensorName(tensor) == "regressors") r_index = i;
    if (cute::tensorName(tensor) == "classificators") c_index = i;
  }

  InitOptions();
  InitAnchors();
}

Detection BlazeFaceWrapper::Run(const Image& image, Angle prior_angle) {
  auto _image = PreProcess(image, prior_angle);

  model.setInput(_image.data);
  model.invoke();

  return PostProcess(prior_angle);
}

Image BlazeFaceWrapper::PreProcess(const Image &image, Angle prior_angle) {
  auto resized_image = ResizeImage(image);
  auto aligned_image = AlignImage(resized_image, prior_angle, target_size);
  return NormalizeImage(aligned_image);
}

Detection BlazeFaceWrapper::PostProcess(Angle prior_angle) {
  static const auto sigmoid_custom = [](auto x) {
    using value_type = decltype(x);
    return static_cast<value_type>(1. / (1. + std::exp(-x)));
  };

  auto raw_boxes = model.getOutput<float>(r_index);
  auto scores = model.getOutput<float>(c_index);
  auto max_index = std::max_element(scores.cbegin(), scores.cend()) - scores.cbegin();

  auto score = static_cast<Score>(sigmoid_custom(scores[max_index]));
  Floats raw_box(raw_boxes.begin() + 16 * max_index, raw_boxes.begin() + 16 * (max_index + 1));
  cv::Point2f anchor = anchors[max_index];

  if (score < threshold) {
    LOGD("Blaze Face : Score under threshold: score=", score);
    return Detection{ROI(), 0, Points()};
  }


  auto [froi, points] = DecodeBox(raw_box, anchor);
  auto [iroi, points_aligned] = RealignOutputs(froi, points, prior_angle);

  return {iroi, score, points_aligned};
}


void BlazeFaceWrapper::InitOptions() {
  scale = 128.0;
  threshold = 0.40;
  num_keypoints = 6;
  keypoint_coord_offset = 4;
}

void BlazeFaceWrapper::InitAnchors() {
  double anchor_offset = 0.5, aspect_ratio = 1.0;
  int input_width = target_size[1], input_height = target_size[0];

  std::vector<int> strides = {8, 16, 16, 16};
  min_scale = 0.1484375;
  num_strides = static_cast<int>(strides.size());

  std::vector<cv::Point2f> anchors_;
  int layer_id = 0;
  while (layer_id < num_strides) {
    auto last_same_stride_layer = layer_id;

    int processing_number = 0;
    while (last_same_stride_layer < strides.size()
    && strides[last_same_stride_layer] == strides[layer_id]) {
      processing_number += 2;
      last_same_stride_layer += 1;
    }

    auto stride = strides[layer_id];
    auto feature_map_height = static_cast<int>(input_height / stride);
    auto feature_map_width = static_cast<int>(input_width / stride);

    for (int y = 0; y < feature_map_height; ++y) {
      for (int x = 0; x < feature_map_width; ++x) {
        auto x_center =
            static_cast<float>(x + anchor_offset) / feature_map_width;
        auto y_center =
            static_cast<float>(y + anchor_offset) / feature_map_height;
        anchors_.insert(anchors_.end(),
                        processing_number,
                        {x_center, y_center});
      }
    }
    layer_id = last_same_stride_layer;
  } // end of while loop

  anchors = std::move(anchors_);
}

//
// Function
//

cv::Mat BlazeFaceWrapper::NormalizeImage(const Image& image) {
  cv::Mat img;
  image.convertTo(img, CV_32F, 1 / 127.5f, -1);
  return img;
}

Image BlazeFaceWrapper::ResizeImage(const Image& image) {
  auto target_ratio = static_cast<double>(target_size[1]) / target_size[0];
  auto input_ratio = static_cast<double>(image.size[1]) / image.size[0];

  int target_width;
  int target_height;
  if (input_ratio >= target_ratio) {
    target_width = target_size[1];
    target_height = static_cast<int>(target_width / input_ratio);
    resize_ratio = static_cast<double>(target_width) / image.size[1];
  } else {
    target_height = target_size[0];
    target_width = static_cast<int>(target_height * input_ratio);
    resize_ratio = static_cast<double>(target_height) / image.size[0];
  }

  Image resized_image;
  cv::resize(image, resized_image, {target_width, target_height});

  int width_diff = target_size[1] - target_width;
  int height_diff = target_size[0] - target_height;
  pad_left = ((0 > width_diff) ? 0 : width_diff) / 2;
  pad_top = ((0 > height_diff) ? 0 : height_diff) / 2;
  auto pad_right = pad_left;
  auto pad_bottom = pad_top;
  pad_right += (width_diff % 2);
  pad_bottom += (height_diff % 2);
  rotation_anchor = {target_size[1] / 2.0, target_size[0] / 2.0};
  cv::copyMakeBorder(resized_image,
                     resized_image,
                     pad_top,
                     pad_bottom,
                     pad_left,
                     pad_right,
                     cv::BORDER_CONSTANT,
                     {0, 0, 0});
  return resized_image;
}

Image BlazeFaceWrapper::AlignImage(const Image& image,
                                                   Angle angle,
                                                   const std::vector<int>& dst_size,
                                                   const ROI& roi) {
  Points src_points;
  Points dst_points;

  cv::Mat modified;
  cv::Point2f pt;

  if (roi.empty()) {
    // Blaze
    pt = cv::Point2f(static_cast<float>(dst_size[1] / 2.0), static_cast<float>(dst_size[0] / 2.0));
    modified = image;

  } else {
    auto target_width = roi[2] - roi[0];
    auto target_height = roi[3] - roi[1];

    // Create rect representing the image
    auto image_rect = cv::Rect({}, image.size());
    auto cropped_roi = cv::Rect(roi[0], roi[1], target_width, target_height);

    // Find intersection, i.e. valid crop region
    auto intersection = image_rect & cropped_roi;

    // Move intersection to the result coordinate space
    auto inter_roi = intersection - cropped_roi.tl();

    modified = cv::Mat::zeros(cropped_roi.size(), image.type());
    image(intersection).copyTo(modified(inter_roi));

    pt = cv::Point2f(static_cast<float>(target_width / 2.), static_cast<float>(target_height / 2.));
  }

  cv::Mat r = cv::getRotationMatrix2D(pt, (angle * 180. / vccc::math_constant::pi<double>), 1.0);

  // Rotate cropped image
  cv::warpAffine(modified, modified, r, cv::Size(dst_size[1], dst_size[0]));

  return modified;
}


FBox BlazeFaceWrapper::DecodeBox(const Floats& raw_box, const cv::Point2f& anchor) const {
  auto x_center = raw_box[0], y_center = raw_box[1];
  auto w = raw_box[2], h = raw_box[3];

  x_center += static_cast<float>(anchor.x * scale);
  y_center += static_cast<float>(anchor.y * scale);

  float ymin = y_center - h / 2.f;
  float xmin = x_center - w / 2.f;
  float ymax = y_center + h / 2.f;
  float xmax = x_center + w / 2.f;

  auto roi = {xmin, ymin, xmax, ymax};
  auto points = Points();

  for (int i = 0; i < num_keypoints; ++i) {
    auto offset = keypoint_coord_offset + i * 2;
    auto keypoint_x = raw_box[offset], keypoint_y = raw_box[offset + 1];
    points.emplace_back(static_cast<float>(keypoint_x + anchor.x * scale),
                        static_cast<float>(keypoint_y + anchor.y * scale));
  }

  return {roi, std::move(points)};
}

Box BlazeFaceWrapper::RealignOutputs(Floats roi,
                                                     const Points& points,
                                                     Angle rotation) {
  roi = {roi[0], roi[1], roi[2], roi[3]};

  auto center_x = (roi[0] + roi[2]) / 2.f;
  auto center_y = (roi[1] + roi[3]) / 2.f;
  auto half_width = center_x - roi[0];
  auto half_height = center_y - roi[1];

  auto x = center_x - rotation_anchor[0];
  auto y = center_y - rotation_anchor[1];
  auto s = std::sin(rotation), c = std::cos(rotation);

  auto x_r = x * c - y * s;
  auto y_r = x * s + y * c;

  auto new_center_x = static_cast<float>(x_r + rotation_anchor[0] - pad_left);
  auto new_center_y = static_cast<float>(y_r + rotation_anchor[1] - pad_top);

  roi = {new_center_x - half_width, new_center_y - half_height,
         new_center_x + half_width, new_center_y + half_height};

  auto _roi = Ints();
  std::transform(roi.begin(), roi.end(), std::back_inserter(_roi),
                 [this](float f) {
    return static_cast<int>(std::round(f / resize_ratio));
  });

  auto _points = Points();
  std::transform(points.begin(), points.end(), std::back_inserter(_points),
                 [this, c, s](const auto& pt) {
    auto x = pt.x - rotation_anchor[0];
    auto y = pt.y - rotation_anchor[1];
    auto x_r = x * c - y * s, y_r = x * s + y * c;

    return cv::Point2f(
        static_cast<float>((x_r + rotation_anchor[0] - pad_left) / resize_ratio),
        static_cast<float>((y_r + rotation_anchor[1] - pad_top) / resize_ratio));
  });

  return {_roi, _points};
}

Angle BlazeFaceWrapper::CalculateFaceAngleFromLandmarks(const Points &face_landmarks) {
  auto right_eye = face_landmarks[0];
  auto left_eye = face_landmarks[1];
  auto angle = std::atan2(left_eye.y - right_eye.y, left_eye.x - right_eye.x);
  return angle;
}
} // namespace vc