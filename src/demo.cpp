#include "image.hpp"

#include <iostream>
#include <stdexcept>
#include <sstream>

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

      // Do something...

      // Save something...
      std::ostringstream out_name;
      out_name << "out_image_" << img_no << ".pgm";
      img.save(out_name.str());
    }
  } catch (std::exception& e) {
    std::cerr << "EXCEPTION: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
