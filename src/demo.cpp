#include "image.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <vector>

//#define ENABLE_MOTION_COMPENSATION
#define ENABLE_FILTER

#ifndef NDEBUG
#define DEBUG_EXPORT_DELTA_IMAGE
#define DEBUG_PRINT_INFO
#ifdef ENABLE_FILTER
#define DEBUG_EXPORT_FILTERED_IMAGE
#endif  // ENABLE_FILTER
#endif  // NDEBUG

namespace {
const int32_t BLOCK_WIDTH = 16;
const int32_t BLOCK_HEIGHT = 8;
const int32_t FRAMES_BETWEEN_FORCED_KEY_BLOCK = 32;

#if defined(ENABLE_MOTION_COMPENSATION)
const int32_t MOTION_DELTA_MIN = -8;
const int32_t MOTION_DELTA_MAX = 7;
#else
const int32_t MOTION_DELTA_MIN = 0;
const int32_t MOTION_DELTA_MAX = 0;
#endif

enum block_type { BLOCK_DELTA_FRAME = 0, BLOCK_DELTA_ROW = 1, BLOCK_COPY = 2 };

int32_t round_up(const int32_t x, const int32_t round_to) {
  return round_to * ((x + round_to - 1) / round_to);
}

#if 0
uint8_t required_bits_old(const int32_t max_delta, const int32_t min_delta) {
  uint32_t v = static_cast<uint32_t>(std::max(max_delta, -min_delta));
  uint8_t num_bits = (v > 0u) ? static_cast<uint8_t>(32 - __builtin_clz(v) + 1) : 0u;
  if (num_bits > 4) {
    num_bits = 8;
  } else if (num_bits > 2) {
    num_bits = 4;
  } else if (num_bits > 0) {
    num_bits = 2;
  }
  return num_bits;
}
#endif

uint8_t required_bits(const uint32_t max_neg_delta, const uint32_t max_pos_delta) {
  uint8_t num_bits;
  if ((max_pos_delta <= 0x00u) && (max_neg_delta >= 0x00000100u)) {
    num_bits = 0u;
  } else if ((max_pos_delta <= 0x00u) && (max_neg_delta >= 0x000000ffu)) {
    num_bits = 1u;
  } else if ((max_pos_delta <= 0x01u) && (max_neg_delta >= 0x000000feu)) {
    num_bits = 2u;
  } else if ((max_pos_delta <= 0x07u) && (max_neg_delta >= 0x000000f8u)) {
    num_bits = 4u;
  } else {
    num_bits = 8u;
  }
  return num_bits;
}

uint8_t get_value_offset(const uint8_t num_bits) {
  static const uint8_t value_offset_tab[9] = {0u, 1u, 2u, 0u, 8u, 0u, 0u, 0u, 0u};
  return value_offset_tab[num_bits];
}

void apply_offset(const uint8_t num_bits, uint8_t* unpacked) {
  const uint8_t offset = get_value_offset(num_bits);
  if (offset > 0u) {
    for (int i = 0; i < 16; ++i) {
      unpacked[i] += offset;
      if (num_bits < 8u && (unpacked[i] & 0x80u)) {
        std::flush(std::cout);
        throw std::runtime_error("WTF?!");
      }
    }
  }
}

void packbits_1(const uint8_t* unpacked, uint8_t*& packed) {
  // Read 16 bytes.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(unpacked);
  uint32_t s1 = src[0];
  uint32_t s2 = src[1];
  uint32_t s3 = src[2];
  uint32_t s4 = src[3];

  // Combine into a single 16-bit word.
  static const uint32_t mask1 = 0x01000000u;
  static const uint32_t mask2 = 0x00010000u;
  static const uint32_t mask3 = 0x00000100u;
  static const uint32_t mask4 = 0x00000001u;
  uint32_t d = ((s1 & mask1) >> 9) | ((s1 & mask2) >> 2) | ((s1 & mask3) << 5) |
               ((s1 & mask4) << 12) | ((s2 & mask1) >> 13) | ((s2 & mask2) >> 8) |
               ((s2 & mask3) << 1) | ((s2 & mask4) << 8) | ((s3 & mask1) >> 17) |
               ((s3 & mask2) >> 10) | ((s3 & mask3) >> 3) | ((s3 & mask4) << 4) |
               ((s4 & mask1) >> 21) | ((s4 & mask2) >> 14) | ((s4 & mask3) >> 7) | (s4 & mask4);

  // Write 2 bytes.
  uint16_t* dst = reinterpret_cast<uint16_t*>(packed);
  dst[0] = static_cast<uint16_t>(d);
  packed += 2;
}

void packbits_2(const uint8_t* unpacked, uint8_t*& packed) {
  // Read 16 bytes.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(unpacked);
  uint32_t s1 = src[0];
  uint32_t s2 = src[1];
  uint32_t s3 = src[2];
  uint32_t s4 = src[3];

  // Combine into a single 32-bit word.
  static const uint32_t mask1 = 0x03000000u;
  static const uint32_t mask2 = 0x00030000u;
  static const uint32_t mask3 = 0x00000300u;
  static const uint32_t mask4 = 0x00000003u;
  uint32_t d = ((s1 & mask1) << 6) | ((s1 & mask2) << 12) | ((s1 & mask3) << 18) |
               ((s1 & mask4) << 24) | ((s2 & mask1) >> 2) | ((s2 & mask2) << 4) |
               ((s2 & mask3) << 10) | ((s2 & mask4) << 16) | ((s3 & mask1) >> 10) |
               ((s3 & mask2) >> 4) | ((s3 & mask3) << 2) | ((s3 & mask4) << 8) |
               ((s4 & mask1) >> 18) | ((s4 & mask2) >> 12) | ((s4 & mask3) >> 6) | (s4 & mask4);

  // Write 4 bytes.
  uint32_t* dst = reinterpret_cast<uint32_t*>(packed);
  dst[0] = d;
  packed += 4;
}

void packbits_4(const uint8_t* unpacked, uint8_t*& packed) {
  // Read 16 bytes.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(unpacked);
  uint32_t s1 = src[0];
  uint32_t s2 = src[1];
  uint32_t s3 = src[2];
  uint32_t s4 = src[3];

  // Combine into two 32-bit words.
  static const uint32_t mask1 = 0x0f000000u;
  static const uint32_t mask2 = 0x000f0000u;
  static const uint32_t mask3 = 0x00000f00u;
  static const uint32_t mask4 = 0x0000000fu;
  uint32_t d1 = ((s1 & mask1) << 4) | ((s1 & mask2) << 8) | ((s1 & mask3) << 12) |
                ((s1 & mask4) << 16) | ((s2 & mask1) >> 12) | ((s2 & mask2) >> 8) |
                ((s2 & mask3) >> 4) | (s2 & mask4);
  uint32_t d2 = ((s3 & mask1) << 4) | ((s3 & mask2) << 8) | ((s3 & mask3) << 12) |
                ((s3 & mask4) << 16) | ((s4 & mask1) >> 12) | ((s4 & mask2) >> 8) |
                ((s4 & mask3) >> 4) | (s4 & mask4);

  // Write 8 bytes.
  uint32_t* dst = reinterpret_cast<uint32_t*>(packed);
  dst[0] = d1;
  dst[1] = d2;
  packed += 8;
}

void packbits_8(const uint8_t* unpacked, uint8_t*& packed) {
  // Copy 16 bytes.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(unpacked);
  uint32_t* dst = reinterpret_cast<uint32_t*>(packed);
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
  dst[3] = src[3];
  packed += 16;
}

void unpackbits_1(const uint8_t*& packed, uint8_t* unpacked) {
  // Read 2 bytes.
  const uint16_t* src = reinterpret_cast<const uint16_t*>(packed);
  uint32_t s1 = static_cast<uint32_t>(src[0]);
  packed += 2;

  // Split into four 32-bit words.
  // TODO(m): Implement me!
  uint32_t d1 = s1;
  uint32_t d2 = s1;
  uint32_t d3 = s1;
  uint32_t d4 = s1;

  // Write 16 bytes.
  uint32_t* dst = reinterpret_cast<uint32_t*>(unpacked);
  dst[0] = d1;
  dst[1] = d2;
  dst[2] = d3;
  dst[3] = d4;
}

void unpackbits_2(const uint8_t*& packed, uint8_t* unpacked) {
  // Read 4 bytes.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(packed);
  uint32_t s1 = src[0];
  packed += 4;

  // Split into four 32-bit words.
  // TODO(m): Implement me!
  uint32_t d1 = s1;
  uint32_t d2 = s1;
  uint32_t d3 = s1;
  uint32_t d4 = s1;

  // Write 16 bytes.
  uint32_t* dst = reinterpret_cast<uint32_t*>(unpacked);
  dst[0] = d1;
  dst[1] = d2;
  dst[2] = d3;
  dst[3] = d4;
}

void unpackbits_4(const uint8_t*& packed, uint8_t* unpacked) {
  // Read 8 bytes.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(packed);
  uint32_t s1 = src[0];
  uint32_t s2 = src[1];
  packed += 8;

  // Split into four 32-bit words.
  // TODO(m): Implement me!
  uint32_t d1 = s1;
  uint32_t d2 = s2;
  uint32_t d3 = s1;
  uint32_t d4 = s2;

  // Write 16 bytes.
  uint32_t* dst = reinterpret_cast<uint32_t*>(unpacked);
  dst[0] = d1;
  dst[1] = d2;
  dst[2] = d3;
  dst[3] = d4;
}

void unpackbits_8(const uint8_t*& packed, uint8_t* unpacked) {
  // Copy 16 bytes.
  const uint32_t* src = reinterpret_cast<const uint32_t*>(packed);
  uint32_t* dst = reinterpret_cast<uint32_t*>(unpacked);
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
  dst[3] = src[3];
  packed += 16;
}

void block_frame_delta(const uint8_t* src1,
                       const uint8_t* src2,
                       const int32_t width,
                       const int32_t height,
                       const int32_t src_stride,
                       uint8_t* dst,
                       uint8_t& num_bits) {
  uint32_t max_pos_delta = 0u;
  uint32_t max_neg_delta = 256u;

  // Delta to the previous image.
  for (int32_t y = 0; y < height; ++y) {
    for (int32_t x = 0; x < width; ++x) {
      const uint8_t delta = src2[x] - src1[x];
      dst[x] = delta;
      if (delta & 0x80u) {
        max_neg_delta = std::min(max_neg_delta, static_cast<uint32_t>(delta));
      } else {
        max_pos_delta = std::max(max_pos_delta, static_cast<uint32_t>(delta));
      }
    }
    src1 += src_stride;
    src2 += src_stride;
    dst += BLOCK_WIDTH;
  }

  num_bits = required_bits(max_neg_delta, max_pos_delta);
}

void block_row_delta(const uint8_t* src,
                     const int32_t width,
                     const int32_t height,
                     const int32_t src_stride,
                     uint8_t* dst,
                     uint8_t& num_bits) {
  // The first row is a raw copy.
  for (int32_t x = 0; x < width; ++x) {
    dst[x] = src[x];
  }
  src += src_stride;
  dst += BLOCK_WIDTH;

  uint32_t max_pos_delta = 0u;
  uint32_t max_neg_delta = 256u;

  // All the following rows are delta to the previous row...
  for (int32_t y = 1; y < height; ++y) {
    for (int32_t x = 0; x < width; ++x) {
      const uint8_t delta = src[x] - src[x - src_stride];
      dst[x] = delta;
      if (delta & 0x80u) {
        max_neg_delta = std::min(max_neg_delta, static_cast<uint32_t>(delta));
      } else {
        max_pos_delta = std::max(max_pos_delta, static_cast<uint32_t>(delta));
      }
    }
    src += src_stride;
    dst += BLOCK_WIDTH;
  }

  num_bits = required_bits(max_neg_delta, max_pos_delta);
}

void block_2d_delta(const uint8_t* src,
                    const int32_t width,
                    const int32_t height,
                    const int32_t src_stride,
                    uint8_t* dst,
                    uint8_t& num_bits) {
  uint32_t max_pos_delta = 0u;
  uint32_t max_neg_delta = 256u;

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
        if (delta & 0x80u) {
          max_neg_delta = std::min(max_neg_delta, static_cast<uint32_t>(delta));
        } else {
          max_pos_delta = std::max(max_pos_delta, static_cast<uint32_t>(delta));
        }
      }
    }
    src += src_stride;
    dst += BLOCK_WIDTH;
  }

  num_bits = required_bits(max_neg_delta, max_pos_delta);
}

void block_copy(const uint8_t* src,
                const int32_t width,
                const int32_t height,
                const int32_t src_stride,
                uint8_t* dst,
                uint8_t& num_bits) {
  for (int32_t y = 0; y < height; ++y) {
    std::memcpy(dst, src, static_cast<size_t>(width));
    src += src_stride;
    dst += BLOCK_WIDTH;
  }

  num_bits = 8u;
}

void pack_int32(const int32_t x, uint8_t* data) {
  data[0] = static_cast<uint8_t>(x);
  data[1] = static_cast<uint8_t>(x >> 8);
  data[2] = static_cast<uint8_t>(x >> 16);
  data[3] = static_cast<uint8_t>(x >> 24);
}

void write_header(const int32_t num_images,
                  const int32_t width,
                  const int32_t height,
                  std::ofstream& packed_file) {
  // Signature.
  packed_file << "LOMC\1";

  uint8_t x4[4];

  // Width.
  pack_int32(width, x4);
  packed_file.write(reinterpret_cast<const char*>(x4), 4);

  // Height.
  pack_int32(height, x4);
  packed_file.write(reinterpret_cast<const char*>(x4), 4);

  // Num images.
  pack_int32(num_images, x4);
  packed_file.write(reinterpret_cast<const char*>(x4), 4);
}
}  // namespace

int main(int argc, const char** argv) {
  try {
    const int32_t num_images = argc - 1;
    if (num_images < 1) {
      throw std::runtime_error("No input files provided.");
    }

    // Read the first image to detrmine the movie properties.
    int32_t width;
    int32_t height;
    {
      lomc::image first_img;
      first_img.load(argv[1]);
      width = first_img.width();
      height = first_img.height();
    }
    const int32_t out_stride = round_up(width, BLOCK_WIDTH);
    const int32_t num_blocks =
        ((width + BLOCK_WIDTH - 1) / BLOCK_WIDTH) * ((height + BLOCK_HEIGHT - 1) / BLOCK_HEIGHT);

#ifdef DEBUG_PRINT_INFO
    std::cout << "Dimensions: " << width << "x" << height << "\n";
    std::cout << "# frames: " << num_images << "\n";
    std::cout << "# blocks / frame: " << num_blocks << "\n";
#endif

    // Create the output file.
    std::ofstream packed_file("packed.lmc", std::ios::out | std::ios::binary);
    write_header(num_images, width, height, packed_file);

    // Create a working buffer for packed data.
    const int32_t control_data_size = round_up(num_blocks, BLOCK_WIDTH);
    std::vector<uint8_t> packed_frame_data(
        static_cast<size_t>(4 + control_data_size + (out_stride * height)));

#ifdef ENABLE_FILTER
    lomc::image filter_image(width, height);
#endif

    // Pack all images.
    int64_t total_packed_size = 0;
    lomc::image images[2];
    for (int32_t img_no = 0u; img_no < num_images; ++img_no) {
      lomc::image& img = images[img_no % 2];

      // Load the image.
      std::string file_name = argv[img_no + 1];
      img.load(file_name);
      if (img.width() != width || img.height() != height) {
        throw std::runtime_error("Incompatible image dimensions!");
      }

#ifdef DEBUG_PRINT_INFO
      std::cout << "Image #" << img_no << ": " << file_name << " (" << img.width() << "x"
                << img.height() << ")\n";
#endif

#ifdef DEBUG_EXPORT_DELTA_IMAGE
      lomc::image delta(img.width(), img.height());
#endif

      int32_t total_bits = 0;

      // Iterate over all the blocks and pack them individually.
      uint8_t* control_data_ptr = packed_frame_data.data() + 4;
      uint8_t* packed_frame_data_ptr = control_data_ptr + control_data_size;
      int32_t block_no = 0;
      for (int32_t y = 0; y < img.height(); y += BLOCK_HEIGHT) {
        const int32_t block_h = std::min(BLOCK_HEIGHT, img.height() - y);
        for (int32_t x = 0; x < img.width(); x += BLOCK_WIDTH) {
          const int32_t block_w = std::min(BLOCK_WIDTH, img.width() - x);

          uint8_t unpacked_block_data_mem[BLOCK_WIDTH * BLOCK_HEIGHT * 2];
          uint8_t* unpacked_block_data[2] = {&unpacked_block_data_mem[0],
                                             &unpacked_block_data_mem[BLOCK_WIDTH * BLOCK_HEIGHT]};

          // Every now and then we force each block to be encoded independently of the previous
          // frame in order to be able to recover from frame losses and similar. From any given
          // frame, it takes FRAMES_BETWEEN_FORCED_KEY_BLOCK until a frame can be fully
          // reconstructed.
          const bool force_key_block =
              (((img_no + block_no) % FRAMES_BETWEEN_FORCED_KEY_BLOCK) == 0);
          const bool can_do_frame_delta = (img_no > 0) && !force_key_block;

          uint8_t best_num_bits = 9;
          int32_t selected_unpacked_block_no = 0;
          block_type bt = BLOCK_COPY;

          // First choice: frame delta.
          if (can_do_frame_delta) {
#ifdef ENABLE_FILTER
            lomc::image& prev_img = filter_image;
#else
            lomc::image& prev_img = images[(img_no + 1) % 2];
#endif
            assert(img.width() == prev_img.width() && img.height() == prev_img.height() &&
                   img.stride() == prev_img.stride());

            // Make a delta to the previous frame. This ususally has the best compression.
            const int32_t min_x_offset = std::max(MOTION_DELTA_MIN, -x);
            const int32_t max_x_offset = std::min(MOTION_DELTA_MAX, width - block_w - x);
            const int32_t min_y_offset = std::max(MOTION_DELTA_MIN, -y);
            const int32_t max_y_offset = std::min(MOTION_DELTA_MAX, height - block_h - y);
            int32_t best_dx = 0;
            int32_t best_dy = 0;
            for (int32_t dy = min_y_offset; dy <= max_y_offset; ++dy) {
              for (int32_t dx = min_x_offset; dx <= max_x_offset; ++dx) {
                int32_t unpacked_block_no = (selected_unpacked_block_no + 1) % 2;
                uint8_t num_bits;
                block_frame_delta(&prev_img[((y + dy) * img.stride()) + (x + dx)],
                                  &img[(y * img.stride()) + x],
                                  block_w,
                                  block_h,
                                  img.stride(),
                                  unpacked_block_data[unpacked_block_no],
                                  num_bits);
                if (num_bits < best_num_bits) {
                  bt = BLOCK_DELTA_FRAME;
                  best_num_bits = num_bits;
                  selected_unpacked_block_no = unpacked_block_no;
                  best_dx = dx;  // TODO(m): These should be encoded as part of the block meta.
                  best_dy = dy;
                }
              }
            }
          }

          // Second choice: row delta.
          if (best_num_bits > 2) {
            // Do not depend on the previous frame. This does not compress as good.
            int32_t unpacked_block_no = (selected_unpacked_block_no + 1) % 2;
            uint8_t num_bits;
            block_row_delta(&img[(y * img.stride()) + x],
                            block_w,
                            block_h,
                            img.stride(),
                            unpacked_block_data[unpacked_block_no],
                            num_bits);
            if (num_bits < best_num_bits) {
              bt = BLOCK_DELTA_ROW;
              best_num_bits = num_bits;
              selected_unpacked_block_no = unpacked_block_no;
            }
          }

          // Fall back to block copy if we could not pack.
          if (best_num_bits >= 8) {
            int32_t unpacked_block_no = (selected_unpacked_block_no + 1) % 2;
            uint8_t num_bits;
            block_copy(&img[(y * img.stride()) + x],
                       block_w,
                       block_h,
                       img.stride(),
                       unpacked_block_data[unpacked_block_no],
                       num_bits);
            bt = BLOCK_COPY;
            best_num_bits = num_bits;
            selected_unpacked_block_no = unpacked_block_no;
          }

#ifdef ENABLE_FILTER
          if (can_do_frame_delta) {
            for (int32_t i = 0; i < block_h; ++i) {
              int32_t yy = y + i;
              const uint8_t* src_data = &img[(yy * img.stride()) + x];
              uint8_t* dst_data = &filter_image[(yy * filter_image.stride()) + x];
              for (int32_t j = 0; j < block_w; ++j) {
                const uint32_t c1 = static_cast<uint32_t>(dst_data[j]);
                const uint32_t c2 = static_cast<uint32_t>(src_data[j]);
                //dst_data[j] = static_cast<uint8_t>((c1 + c2) >> 1);
                dst_data[j] = static_cast<uint8_t>(((c1 * 3) + c2) >> 2);
              }
            }
          } else {
            // Clear the filtered block: Copy the input image to the filtered image.
            for (int32_t i = 0; i < block_h; ++i) {
              int32_t yy = y + i;
              const uint8_t* src_data = &img[(yy * img.stride()) + x];
              uint8_t* dst_data = &filter_image[(yy * filter_image.stride()) + x];
              for (int32_t j = 0; j < block_w; ++j) {
                dst_data[j] = src_data[j];
              }
            }
          }
#endif

#if 0 && defined(DEBUG_PRINT_INFO)
          static int32_t num_bits_histogram[10];
          num_bits_histogram[best_num_bits]++;
          if ((block_no % 100) == 0) {
            for (int i = 0; i < 9; ++i) {
              std::cout << num_bits_histogram[i] << " ";
            }
            std::cout << num_bits_histogram[9] << "\n";
          }
#endif

          total_bits += static_cast<int32_t>(best_num_bits);

          // Output the control byte for this block.
          uint8_t control_byte = static_cast<uint8_t>(bt << 4) | best_num_bits;
          control_data_ptr[block_no] = control_byte;

          // Output the packed pixel deltas.
          // Special case: BLOCK_DELTA_ROW always uses 8 bits for the first row.
          uint8_t num_bits_for_next_row = (bt == BLOCK_DELTA_ROW) ? 8 : best_num_bits;
          uint8_t* src_data = unpacked_block_data[selected_unpacked_block_no];
          for (int32_t row = 0; row < block_h; ++row) {
            apply_offset(num_bits_for_next_row, src_data);
            switch (num_bits_for_next_row) {
              case 1u:
                packbits_1(src_data, packed_frame_data_ptr);
                break;
              case 2u:
                packbits_2(src_data, packed_frame_data_ptr);
                break;
              case 4u:
                packbits_4(src_data, packed_frame_data_ptr);
                break;
              case 8u:
                packbits_8(src_data, packed_frame_data_ptr);
                break;
              case 0u:
                break;
              default:
                throw std::runtime_error("Invalid num_bits");
            }
            src_data += BLOCK_WIDTH;
            num_bits_for_next_row = best_num_bits;
          }

#ifdef DEBUG_EXPORT_DELTA_IMAGE
          // Copy the unpacked block data to the delta image (for debugging).
          for (int32_t i = 0; i < block_h; ++i) {
            int32_t yy = y + i;
            const uint8_t* src_data = unpacked_block_data[selected_unpacked_block_no];
            for (int32_t j = 0; j < block_w; ++j) {
              int32_t xx = x + j;
              delta[(yy * delta.stride()) + xx] = src_data[(i * BLOCK_WIDTH) + j];
            }
          }
#endif
          ++block_no;
        }
      }

      // Append the packed data to the output stream.
      int32_t packed_frame_size =
          static_cast<int32_t>(reinterpret_cast<intptr_t>(packed_frame_data_ptr) -
                               reinterpret_cast<intptr_t>(packed_frame_data.data()));
      pack_int32(packed_frame_size, &packed_frame_data[0]);
      packed_file.write(reinterpret_cast<const char*>(packed_frame_data.data()), packed_frame_size);
      total_packed_size += static_cast<int64_t>(packed_frame_size);

#ifdef DEBUG_PRINT_INFO
      std::cout << "Frame size: " << packed_frame_size << "\n";
      std::cout << "Average bits: "
                << static_cast<double>(total_bits) / static_cast<double>(num_blocks) << "\n";
#endif

#ifdef DEBUG_EXPORT_DELTA_IMAGE
      std::ostringstream delta_file_name;
      delta_file_name << "out_delta_" << std::setfill('0') << std::setw(4) << img_no << ".pgm";
      delta.save(delta_file_name.str());
#endif
#ifdef DEBUG_EXPORT_FILTERED_IMAGE
      std::ostringstream filtered_file_name;
      filtered_file_name << "out_filt_" << std::setfill('0') << std::setw(4) << img_no << ".pgm";
      filter_image.save(filtered_file_name.str());
#endif
    }

#ifdef DEBUG_PRINT_INFO
    const int64_t total_unpacked_size =
        static_cast<int64_t>(num_images) * static_cast<int64_t>(width * height);
    const double compression_ratio =
        static_cast<double>(total_packed_size) / static_cast<double>(total_unpacked_size);
    std::cout << "Compression ratio: " << (100.0 * compression_ratio) << "%\n";
#endif

    // Close the output file.
    packed_file.close();
  } catch (std::exception& e) {
    std::cerr << "EXCEPTION: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
