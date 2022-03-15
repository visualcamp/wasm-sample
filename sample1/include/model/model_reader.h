//
// Created by elon on 2021/08/18.
//

#ifndef WASMSAMPLE_MODEL_MODEL_READER_H_
#define WASMSAMPLE_MODEL_MODEL_READER_H_

namespace vc {

class ModelReader {
 public:
  using buffer_type = char*;

  struct ModelData {
    buffer_type byte;
    unsigned int size;
  };

  static ModelData ReadBlazeFaceModel();
};
}
#endif //WASMSAMPLE_MODEL_MODEL_READER_H_
