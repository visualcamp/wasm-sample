//
// Created by elon on 2021/08/18.
//

#include "model_reader.h"
#include "model/blaze_face_model.h"
namespace vc{
ModelReader::ModelData ModelReader::ReadBlazeFaceModel() {
  return {(buffer_type) blaze_face_model_tflite, blaze_face_model_tflite_len};
}
}