/*

Copyright (c) 2015, Project OSRM contributors
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "raster_source.hpp"

#include "../util/simple_logger.hpp"
#include "../util/timing_util.hpp"

#include <osrm/coordinate.hpp>

#include <unordered_map>
#include <sstream>
#include <cmath>
#include <iostream>

// LoadedSources stores sources in memory; LoadedSourcePaths maps them to indices
std::vector<RasterSource> LoadedSources;
std::unordered_map<std::string, int> LoadedSourcePaths;

RasterSource::RasterSource(RasterGrid _raster_data,
                           std::size_t _width,
                           std::size_t _height,
                           double _xmin,
                           double _xmax,
                           double _ymin,
                           double _ymax)
    : xstep(calcSize(_xmin, _xmax, _width)), ystep(calcSize(_ymin, _ymax, _height)),
      raster_data(_raster_data), width(_width), height(_height), xmin(_xmin), xmax(_xmax),
      ymin(_ymin), ymax(_ymax)
{
    BOOST_ASSERT(xstep != 0);
    BOOST_ASSERT(ystep != 0);
}

float RasterSource::calcSize(double min, double max, unsigned count) const
{
    BOOST_ASSERT(count > 0);
    return (max - min) / count;
}

// Query raster source for nearest data point
RasterDatum RasterSource::getRasterData(const float lon, const float lat) const
{
    if (lon < xmin || lon > xmax || lat < ymin || lat > ymax)
    {
        return {};
    }

    unsigned xthP = (lon - xmin) / xstep;
    int xth = ((xthP - floor(xthP)) > (xstep / 2) ? floor(xthP) : ceil(xthP));

    unsigned ythP = (ymax - lat) / ystep;
    int yth = ((ythP - floor(ythP)) > (ystep / 2) ? floor(ythP) : ceil(ythP));

    return {raster_data(xth, yth)};
}

// Query raster source using bilinear interpolation
RasterDatum RasterSource::getRasterInterpolate(const float lon, const float lat) const
{
    if (lon < xmin || lon > xmax || lat < ymin || lat > ymax)
    {
        return {};
    }

    unsigned xthP = (lon - xmin) / xstep;
    unsigned ythP = (ymax - lat) / ystep;
    int top = floor(ythP);
    int bottom = ceil(ythP);
    int left = floor(xthP);
    int right = ceil(xthP);

    float x = (lon - left * xstep + xmin) / xstep;
    float y = (ymax - top * ystep - lat) / ystep;
    float x1 = 1.0 - x;
    float y1 = 1.0 - y;

    return {static_cast<std::int32_t>(
        raster_data(left, top) * (x1 * y1) + raster_data(right, top) * (x * y1) +
        raster_data(left, bottom) * (x1 * y) + raster_data(right, bottom) * (x * y))};
}

// Load raster source into memory
int loadRasterSource(const std::string &source_path,
                     double xmin,
                     double xmax,
                     double ymin,
                     double ymax,
                     unsigned nrows,
                     unsigned ncols)
{
    auto itr = LoadedSourcePaths.find(source_path);
    if (itr != LoadedSourcePaths.end())
    {
        std::cout << "[source loader] Already loaded source '" << source_path << "' at source_id "
                  << itr->second << std::endl;
        return itr->second;
    }

    int source_id = LoadedSources.size();

    std::cout << "[source loader] Loading from " << source_path << "  ... " << std::flush;
    TIMER_START(loading_source);

    if (!boost::filesystem::exists(source_path.c_str()))
    {
        throw osrm::exception("error reading: no such path");
    }

    RasterGrid rasterData{source_path, ncols, nrows};

    RasterSource source{rasterData,
                        static_cast<std::size_t>(ncols),
                        static_cast<std::size_t>(nrows),
                        xmin,
                        xmax,
                        ymin,
                        ymax};
    TIMER_STOP(loading_source);
    LoadedSourcePaths.emplace(source_path, source_id);
    LoadedSources.push_back(std::move(source));

    std::cout << "ok, after " << TIMER_SEC(loading_source) << "s" << std::endl;

    return source_id;
}

// External function for looking up nearest data point from a specified source
RasterDatum getRasterDataFromSource(unsigned int source_id, int lon, int lat)
{
    if (LoadedSources.size() < source_id + 1)
    {
        throw osrm::exception("error reading: no such loaded source");
    }

    const auto &found = LoadedSources[source_id];
    return found.getRasterData(float(lon) / COORDINATE_PRECISION,
                               float(lat) / COORDINATE_PRECISION);
}

// External function for looking up interpolated data from a specified source
RasterDatum getRasterInterpolateFromSource(unsigned int source_id, int lon, int lat)
{
    if (LoadedSources.size() < source_id + 1)
    {
        throw osrm::exception("error reading: no such loaded source");
    }

    const auto &found = LoadedSources[source_id];
    return found.getRasterInterpolate(float(lon) / COORDINATE_PRECISION,
                                      float(lat) / COORDINATE_PRECISION);
}
