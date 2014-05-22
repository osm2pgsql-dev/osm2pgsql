#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace util {
	inline int double_to_fix(const double x, const double scale) {
		return int((x) * scale + 0.4);
	}
	inline double fix_to_double(const int x, const int scale) {
		return (double)x / scale;
	}
	void exit_nicely();
}


#endif
