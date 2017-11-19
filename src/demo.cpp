#include "image.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <sstream>

namespace {
const int32_t BLOCK_WIDTH = 16;
const int32_t BLOCK_HEIGHT = 8;

uint8_t required_bits(const int32_t max_delta, const int32_t min_delta) {
  uint32_t v = static_cast<uint32_t>(std::max(max_delta, -min_delta));
  uint8_t num_bits = (v > 0u) ? static_cast<uint8_t>(32 - __builtin_clz(v) + 1) : 0u;
  if (num_bits > 4) {
    num_bits = 8;
  } else if(num_bits > 2) {
    num_bits = 4;
  } else if(num_bits > 0) {
    num_bits = 2;
  }
  return num_bits;
}

void block_row_delta(const uint8_t* src,
                     const int32_t width,
                     const int32_t height,
                     const int32_t src_stride,
                     const int32_t dst_stride,
                     uint8_t* dst,
                     uint8_t& num_bits) {
  // The first row is a raw copy.
  for (int32_t x = 0; x < width; ++x) {
    dst[x] = src[x];
  }
  src += src_stride;
  dst += dst_stride;

  int8_t max_delta = -128;
  int8_t min_delta = 127;

  // All the following rows are delta to the previous row...
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

void block_2d_delta(const uint8_t* src,
                    const int32_t width,
                    const int32_t height,
                    const int32_t src_stride,
                    const int32_t dst_stride,
                    uint8_t* dst,
                    uint8_t& num_bits) {
  int8_t max_delta = -128;
  int8_t min_delta = 127;

  for (int32_t y = 0; y < height; ++y) {
    for (int32_t x = 0; x < width; ++x) {
      uint8_t predicted;
      if (x > 0 && y > 0) {
        predicted = (src[x - 1] + src[x - src_stride]) - src[x - src_stride - 1];
      } else if (x > 0) {
        predicted = src[x - 1];
      } else if (y > 0) {
        predicted = src[x - src_stride];
      } else {
        predicted = 0;
      }
      const uint8_t delta = src[x] - predicted;
      dst[x] = delta;
      if (x > 0 || y > 0) {
        max_delta = std::max(max_delta, static_cast<int8_t>(delta));
        min_delta = std::min(min_delta, static_cast<int8_t>(delta));
      }
    }
    src += src_stride;
    dst += dst_stride;
  }

  num_bits = required_bits(max_delta, min_delta);
}

void block_frame_delta(const uint8_t* src1,
                       const uint8_t* src2,
                       const int32_t width,
                       const int32_t height,
                       const int32_t src_stride,
                       const int32_t dst_stride,
                       uint8_t* dst,
                       uint8_t& num_bits) {
  int8_t max_delta = -128;
  int8_t min_delta = 127;

  // All the following rows are delta to the previous row...
  for (int32_t y = 0; y < height; ++y) {
    for (int32_t x = 0; x < width; ++x) {
      const uint8_t delta = src2[x] - src1[x];
      dst[x] = delta;
      max_delta = std::max(max_delta, static_cast<int8_t>(delta));
      min_delta = std::min(min_delta, static_cast<int8_t>(delta));
    }
    src1 += src_stride;
    src2 += src_stride;
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
          uint8_t num_bits;
          if (img_no == 0) {
            block_row_delta(&img[(y * img.stride()) + x],
                            block_w,
                            block_h,
                            img.stride(),
                            delta.stride(),
                            &delta[(y * delta.stride()) + x],
                            num_bits);
          } else {
            lomc::image& prev_img = images[(img_no + 1) % 2];
            assert(img.width() == prev_img.width() && img.height() == prev_img.height() &&
                   img.stride() == prev_img.stride());
            block_frame_delta(&prev_img[(y * img.stride()) + x],
                              &img[(y * img.stride()) + x],
                              block_w,
                              block_h,
                              img.stride(),
                              delta.stride(),
                              &delta[(y * delta.stride()) + x],
                              num_bits);
          }
          total_bits += static_cast<int32_t>(num_bits);
          ++total_blocks;
          // std::cout << "b=" << static_cast<int32_t>(num_bits) << "\n";
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
