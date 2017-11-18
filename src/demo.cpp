#include "image.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <sstream>

namespace {
const int32_t BLOCK_WIDTH = 16;
const int32_t BLOCK_HEIGHT = 8;

uint8_t required_bits(const int32_t max_delta, const int32_t min_delta) {
  uint32_t v = static_cast<uint32_t>(std::max(max_delta, -min_delta));
  return (v > 0u) ? static_cast<uint8_t>(32 - __builtin_clz(v) + 1) : 0u;
}

void block_row_delta(const uint8_t* src,
                     const int32_t width,
                     const int32_t height,
                     const int32_t src_stride,
                     const int32_t dst_stride,
                     uint8_t* dst,
                     uint8_t& start_value,
                     uint8_t& num_bits) {
  // Calculate the average pixel value of the first row.
  int32_t row_sum = 0;
  for (int32_t x = 0; x < width; ++x) {
    row_sum += static_cast<int32_t>(src[x]);
  }
  start_value = static_cast<uint8_t>((row_sum + (width / 2)) / width);

  int8_t max_delta = -128;
  int8_t min_delta = 127;

  // The first row.
  for (int32_t x = 0; x < width; ++x) {
    const uint8_t delta = src[x] - start_value;
    dst[x] = delta;
    max_delta = std::max(max_delta, static_cast<int8_t>(delta));
    min_delta = std::min(min_delta, static_cast<int8_t>(delta));
  }
  src += src_stride;
  dst += dst_stride;

  // All the following rows...
  for (int32_t y = 1; y < height; ++y) {
    for (int32_t x = 0; x < width; ++x) {
      const uint8_t delta = src[x] - src[x - src_stride];
      dst[x] = delta;
      max_delta = std::max(max_delta, static_cast<int8_t>(delta));
      min_delta = std::min(min_delta, static_cast<int8_t>(delta));
    }
    src += src_stride;
    dst += dst_stride;
  }

  num_bits = required_bits(max_delta, min_delta);
}

}  // namespace

int main(int argc, const char** argv) {
  try {
    lomc::image images[2];
    const int num_images = argc - 1;
    for (int img_no = 0; img_no < num_images; ++img_no) {
      lomc::image& img = images[img_no % 2];

      // Load the image.
      std::string file_name = argv[img_no + 1];
      std::cout << "Image #" << img_no << ": " << file_name << "\n";
      img.load(file_name);
      std::cout << "   width: " << img.width() << "\n";
      std::cout << "  height: " << img.height() << "\n";
      std::cout << "  stride: " << img.stride() << "\n";

      // Generate the delta image.
      lomc::image delta(img.width(), img.height());
      int32_t total_bits = 0;
      int32_t total_blocks = 0;
      for (int32_t y = 0; y < img.height(); y += BLOCK_HEIGHT) {
        const int32_t block_h = std::min(BLOCK_HEIGHT, img.height() - y);
        for (int32_t x = 0; x < img.width(); x += BLOCK_WIDTH) {
          const int32_t block_w = std::min(BLOCK_WIDTH, img.width() - x);
          uint8_t start_value;
          uint8_t num_bits;
          block_row_delta(&img[(y * img.stride()) + x],
                          block_w,
                          block_h,
                          img.stride(),
                          delta.stride(),
                          &delta[(y * delta.stride()) + x],
                          start_value,
                          num_bits);
          total_bits += static_cast<int32_t>(num_bits);
          ++total_blocks;
          // std::cout << "s=" << static_cast<int32_t>(start_value)
          //           << ", b=" << static_cast<int32_t>(num_bits) << "\n";
        }
      }
      std::cout << "average bits="
                << static_cast<double>(total_bits) / static_cast<double>(total_blocks) << "\n";

      // Save something...
      std::ostringstream out_name;
      out_name << "out_image_" << img_no << ".pgm";
      delta.save(out_name.str());
    }
  } catch (std::exception& e) {
    std::cerr << "EXCEPTION: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
