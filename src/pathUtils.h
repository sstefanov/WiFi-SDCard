#ifndef _PATH_UTILS_H_
#define _PATH_UTILS_H_

#include <Arduino.h>

// Resolves `arg` against `base` into a normalized absolute path, collapsing
// "." and ".." segments. Never escapes above root ("/").
inline String normalizeDirPath(const String &base, const String &arg) {
    String combined;
    if (arg.length() > 0 && arg[0] == '/') {
        combined = arg;
    } else {
        combined = base;
        if (!combined.endsWith("/"))
            combined += "/";
        combined += arg;
    }

    static const int MAX_SEGMENTS = 32;
    String segments[MAX_SEGMENTS];
    int count = 0;

    int i = 0;
    int n = combined.length();
    while (i < n) {
        while (i < n && combined[i] == '/')
            i++;
        int start = i;
        while (i < n && combined[i] != '/')
            i++;
        if (i > start) {
            String seg = combined.substring(start, i);
            if (seg == ".") {
                // skip
            } else if (seg == "..") {
                if (count > 0)
                    count--;
            } else if (count < MAX_SEGMENTS) {
                segments[count++] = seg;
            }
        }
    }

    String result = "/";
    for (int j = 0; j < count; j++) {
        result += segments[j];
        if (j < count - 1)
            result += "/";
    }
    return result;
}

#endif
