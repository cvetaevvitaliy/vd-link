#pragma once

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <sys/stat.h>

// Converts UTF-8 string to UTF-16 (wide char)
wchar_t* utf8_to_utf16(const char* utf8_str);

// Frees memory allocated for UTF-16 string
void free_utf16(wchar_t* utf16_str);

// Unicode-compatible fopen for Windows
FILE* unicode_fopen(const char* filename, const char* mode);

// Unicode-compatible stat for Windows
int unicode_stat(const char* filename, struct stat* st);

#else
// On other platforms, simply use standard functions
#define unicode_fopen(filename, mode) fopen(filename, mode)
#define unicode_stat(filename, st) stat(filename, st)
#endif