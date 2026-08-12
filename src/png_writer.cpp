#include "dicom_processor/png_writer.hpp"
#include <png.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace dicom
{

    namespace
    {

        uint8_t windowPixel(float hu, double windowMin, double windowMax)
        {
            const double t = (hu - windowMin) / (windowMax - windowMin);
            return static_cast<uint8_t>(std::clamp(t, 0.0, 1.0) * 255.0 + 0.5);
        }

        void drawRectOutline(std::vector<uint8_t> &rgb, int width, int height, int x0, int y0, int x1, int y1)
        {
            auto setPx = [&](int x, int y)
            {
                if (x < 0 || x >= width || y < 0 || y >= height)
                    return;
                const size_t idx = (static_cast<size_t>(y) * width + x) * 3;
                rgb[idx] = 255;
                rgb[idx + 1] = 40;
                rgb[idx + 2] = 40;
            };
            for (int x = x0; x <= x1; ++x)
            {
                setPx(x, y0);
                setPx(x, y1);
            }
            for (int y = y0; y <= y1; ++y)
            {
                setPx(x0, y);
                setPx(x1, y);
            }
        }

        // Shared libpng write sequence. `bytesPerPixel` and `colorType` select
        // grayscale vs RGB; `pixels` must already be in that layout, row-major.
        void writePng(const std::string &path, int width, int height, int colorType, int bytesPerPixel,
                      const std::vector<uint8_t> &pixels)
        {
            FILE *fp = std::fopen(path.c_str(), "wb");
            if (!fp)
                throw std::runtime_error("Could not open PNG output file: " + path);

            png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
            if (!png)
            {
                std::fclose(fp);
                throw std::runtime_error("png_create_write_struct failed");
            }

            png_infop info = png_create_info_struct(png);
            if (!info)
            {
                png_destroy_write_struct(&png, nullptr);
                std::fclose(fp);
                throw std::runtime_error("png_create_info_struct failed");
            }

            if (setjmp(png_jmpbuf(png)))
            {
                png_destroy_write_struct(&png, &info);
                std::fclose(fp);
                throw std::runtime_error("libpng error while writing: " + path);
            }

            png_init_io(png, fp);
            png_set_IHDR(png, info, static_cast<png_uint_32>(width), static_cast<png_uint_32>(height), 8,
                         colorType, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
            png_write_info(png, info);

            std::vector<png_bytep> rows(static_cast<size_t>(height));
            for (int y = 0; y < height; ++y)
            {
                rows[static_cast<size_t>(y)] =
                    const_cast<png_bytep>(pixels.data() + static_cast<size_t>(y) * width * bytesPerPixel);
            }
            png_write_image(png, rows.data());
            png_write_end(png, nullptr);

            png_destroy_write_struct(&png, &info);
            std::fclose(fp);
        }

    } // namespace

    void PngWriter::writeSlice(const VoxelVolume &volume, int z, const std::string &outputPath,
                               double windowMin, double windowMax)
    {
        if (z < 0 || z >= volume.depth)
            throw std::runtime_error("PngWriter::writeSlice: z out of range");

        const int w = volume.width, h = volume.height;
        std::vector<uint8_t> gray(static_cast<size_t>(w) * h);
        const float *src = volume.slicePtr(z);
        for (size_t i = 0; i < gray.size(); ++i)
        {
            gray[i] = windowPixel(src[i], windowMin, windowMax);
        }
        writePng(outputPath, w, h, PNG_COLOR_TYPE_GRAY, 1, gray);
    }

    void PngWriter::writeSliceAnnotated(const VoxelVolume &volume, int z, const std::vector<Anomaly> &findings,
                                        const std::string &outputPath, double windowMin, double windowMax)
    {
        if (z < 0 || z >= volume.depth)
            throw std::runtime_error("PngWriter::writeSliceAnnotated: z out of range");

        const int w = volume.width, h = volume.height;
        std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
        const float *src = volume.slicePtr(z);
        for (int i = 0; i < w * h; ++i)
        {
            const uint8_t v = windowPixel(src[i], windowMin, windowMax);
            rgb[static_cast<size_t>(i) * 3] = v;
            rgb[static_cast<size_t>(i) * 3 + 1] = v;
            rgb[static_cast<size_t>(i) * 3 + 2] = v;
        }

        for (const auto &a : findings)
        {
            if (z < a.bboxMin[2] || z > a.bboxMax[2])
                continue;
            drawRectOutline(rgb, w, h, a.bboxMin[0], a.bboxMin[1], a.bboxMax[0], a.bboxMax[1]);
        }

        writePng(outputPath, w, h, PNG_COLOR_TYPE_RGB, 3, rgb);
    }

} // namespace dicom
