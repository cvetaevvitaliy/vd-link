#include "unicode_utils.h"
#include <stdlib.h>

#ifdef _WIN32

wchar_t* utf8_to_utf16(const char* utf8_str) {
    if (!utf8_str) {
        return NULL;
    }
    
    // First, determine the required buffer size
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
    if (wide_len <= 0) {
        return NULL;
    }
    
    // Allocate memory for UTF-16 string
    wchar_t* wide_str = malloc(wide_len * sizeof(wchar_t));
    if (!wide_str) {
        return NULL;
    }
    
    // Perform conversion
    int result = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wide_str, wide_len);
    if (result <= 0) {
        free(wide_str);
        return NULL;
    }
    
    return wide_str;
}

void free_utf16(wchar_t* utf16_str) {
    if (utf16_str) {
        free(utf16_str);
    }
}

FILE* unicode_fopen(const char* filename, const char* mode) {
    wchar_t* wide_filename = utf8_to_utf16(filename);
    wchar_t* wide_mode = utf8_to_utf16(mode);
    
    if (!wide_filename || !wide_mode) {
        free_utf16(wide_filename);
        free_utf16(wide_mode);
        return NULL;
    }
    
    FILE* file = _wfopen(wide_filename, wide_mode);
    
    free_utf16(wide_filename);
    free_utf16(wide_mode);
    
    return file;
}

int unicode_stat(const char* filename, struct stat* st) {
    wchar_t* wide_filename = utf8_to_utf16(filename);
    if (!wide_filename) {
        return -1;
    }
    
    int result = _wstat(wide_filename, (struct _stat*)st);
    
    free_utf16(wide_filename);
    
    return result;
}

#endif