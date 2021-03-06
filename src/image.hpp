#ifndef IMAGE_HPP_
#define IMAGE_HPP_

#include <tinypgm.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace lomc {
class image {
public:
  image() : width_(0), height_(0), stride_(0) {
  }

  explicit image(const std::string& file_name) {
    load(file_name);
  }

  image(const int32_t width, const int32_t height)
      : pixels_(static_cast<size_t>(optimal_stride(width) * height)),
        width_(width),
        height_(height),
        stride_(optimal_stride(width)) {
  }

  void load(const std::string& file_name) {
    // Get image information.
    tpgm_info_t info;
    if (!tpgm_load_info(file_name.c_str(), &info)) {
      throw std::runtime_error("Failed to load image");
    }
    width_ = info.width;
    height_ = info.height;
    stride_ = info.width;

    // Load the image data.
    pixels_.resize(info.data_size);
    if (!tpgm_load_data(file_name.c_str(), NULL, pixels_.data(), info.data_size)) {
      throw std::runtime_error("Failed to load image data");
    }
  }

  void save(const std::string& file_name) {
    if (!tpgm_save(file_name.c_str(), pixels_.data(), width_, height_, stride_)) {
      throw std::runtime_error("Failed to save image");
    }
  }

  template <typename INDEX_T>
  uint8_t& operator[](const INDEX_T index) {
    return pixels_[index];
  }

  template <typename INDEX_T>
  const uint8_t& operator[](const INDEX_T index) const {
    return pixels_[index];
  }

  int32_t width() const {
    return width_;
  }

  int32_t height() const {
    return height_;
  }

  int32_t stride() const {
    return stride_;
  }

private:
  static const int32_t OPTIMAL_ALIGNMENT = 16;

  static int32_t optimal_stride(const int32_t width) {
    return OPTIMAL_ALIGNMENT * ((width + OPTIMAL_ALIGNMENT - 1) / OPTIMAL_ALIGNMENT);
  }

  std::vector<uint8_t> pixels_;
  int32_t width_;
  int32_t height_;
  int32_t stride_;
};
}  // namespace lomc

#endif  // IMAGE_HPP_
