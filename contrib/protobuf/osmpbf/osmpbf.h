#ifndef OSMPBF_H
#define OSMPBF_H

// this describes the low-level blob storage
#include <osmpbf/fileformat.pb.h> // IWYU pragma: export

// this describes the high-level OSM objects
#include <osmpbf/osmformat.pb.h> // IWYU pragma: export

#define OSMPBF_VERSION "1.3.3"

namespace OSMPBF {

    // the maximum size of a blob header in bytes
    const int max_blob_header_size = 64 * 1024; // 64 kB

    // the maximum size of an uncompressed blob in bytes
    const int max_uncompressed_blob_size = 32 * 1024 * 1024; // 32 MB

    // resolution for longitude/latitude used for conversion
    // between representation as double and as int
    const int lonlat_resolution = 1000 * 1000 * 1000; 

}

#endif // OSMPBF_H
