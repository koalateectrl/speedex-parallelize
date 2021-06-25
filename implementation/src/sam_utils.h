#pragma once

#include <vector>
#include <xdrpp/marshal.h>
#include <chrono>

namespace sam_edce {

using time_point = std::chrono::time_point<std::chrono::steady_clock>;

template <typename Clock>
inline static double time_diff(const Clock& start, const Clock& end) {
    return ((double)std::chrono::duration_cast<std::chrono::microseconds>(end-start).count()) / 1000000;
}

inline static time_point init_time_measurement() {
    return std::chrono::steady_clock::now();
}

inline static double measure_time(time_point& prev_measurement) {
    auto new_measurement = std::chrono::steady_clock::now();
    auto out = time_diff(prev_measurement, new_measurement);
    prev_measurement = new_measurement;
    return out;
}


template<typename xdr_type>
int __attribute__((warn_unused_result)) load_xdr_from_file(xdr_type& output, const char* filename)  {
    FILE* f = std::fopen(filename, "r");

    if (f == nullptr) {
        return -1;
    }

    std::vector<char> contents;
    const int BUF_SIZE = 65536;
    char buf[BUF_SIZE];

    int count = -1;
    while (count != 0) {
        count = std::fread(buf, sizeof(char), BUF_SIZE, f);
        if (count > 0) {
            contents.insert(contents.end(), buf, buf+count);
        }
    }

    xdr::xdr_from_opaque(contents, output);
    std::fclose(f);
    return 0;
}

}

