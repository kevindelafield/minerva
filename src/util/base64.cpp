/*
 * This code was found to be suitable and in the public domain.  The author is unknown.
 */

#include <iostream>
#include <cstring>
#include "base64.h"
#include "log.h"

namespace minerva
{

    static const char base64digits[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 
    static const char BAD = -1;

    static const char base64val[] = 
    {
        BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
        BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
        BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD, 62, BAD,BAD,BAD, 63,
        52, 53, 54, 55,  56, 57, 58, 59,  60, 61,BAD,BAD, BAD,BAD,BAD,BAD,
        BAD,  0,  1,  2,   3,  4,  5,  6,   7,  8,  9, 10,  11, 12, 13, 14,
        15, 16, 17, 18,  19, 20, 21, 22,  23, 24, 25,BAD, BAD,BAD,BAD,BAD,
        BAD, 26, 27, 28,  29, 30, 31, 32,  33, 34, 35, 36,  37, 38, 39, 40,
        41, 42, 43, 44,  45, 46, 47, 48,  49, 50, 51,BAD, BAD,BAD,BAD,BAD
    };

    inline char DECODE64(unsigned char c) 
    {
        return c < 0x80 ? base64val[c] : BAD;
    }

    std::string to64frombits(const std::string & in)
    {
        std::string out;

        out.clear();
        out.reserve((in.size() * 4 / 3) + 4); // Correct: 3 input bytes -> 4 output bytes

        size_t inlen = in.size();
        size_t count = 0;
        for (; inlen >= 3; inlen -= 3)
        {
            out += base64digits[static_cast<unsigned char>(in[count+0]) >> 2];
            out += 
                base64digits[((static_cast<unsigned char>(in[count+0]) << 4) & 0x30) | 
                             (static_cast<unsigned char>(in[count+1]) >> 4)];
            out += 
                base64digits[((static_cast<unsigned char>(in[count+1]) << 2) & 0x3c) | 
                             (static_cast<unsigned char>(in[count+2]) >> 6)];
            out += base64digits[static_cast<unsigned char>(in[count+2]) & 0x3f];
            count += 3;
        }

        if (inlen > 0)
        {
            unsigned char fragment;

            out += base64digits[static_cast<unsigned char>(in[count+0]) >> 2];
            fragment = (static_cast<unsigned char>(in[count+0]) << 4) & 0x30;

            if (inlen > 1) fragment |= static_cast<unsigned char>(in[count+1]) >> 4;

            out += base64digits[fragment];
            out += (inlen < 2) ? '=' : base64digits[(static_cast<unsigned char>(in[count+1]) << 2) & static_cast<unsigned char>(0x3c)];
            out += '=';
        }

        return out;
    }
 
    std::string to64frombits(const std::vector<unsigned char> & in)
    {
        std::string out;

        out.clear();
        out.reserve((in.size() * 4 / 3) + 4); // Correct: 3 input bytes -> 4 output bytes

        size_t inlen = in.size();
        size_t count = 0;
        for (; inlen >= 3; inlen -= 3)
        {
            out += base64digits[in[count+0] >> 2];
            out += 
                base64digits[(in[count+0] << 4) & 0x30 | 
                             (in[count+1] >> 4)];
            out += 
                base64digits[(in[count+1] << 2) & 0x3c | 
                             (in[count+2] >> 6)];
            out += base64digits[in[count+2] & 0x3f];
            count += 3;
        }

        if (inlen > 0)
        {
            unsigned char fragment;

            out += base64digits[in[count+0] >> 2];
            fragment = (in[count+0] << 4) & 0x30;

            if (inlen > 1) fragment |= in[count+1] >> 4;

            out += base64digits[fragment];
            out += (inlen < 2) ? '=' : 
                base64digits[(in[count+1] << 2) & 
                             static_cast<unsigned char>(0x3c)];
            out += '=';
        }

        return out;
    }
 
    bool from64tobits(const std::string & in, std::string & out)
    {
        out.clear();
        
        if (in.empty()) {
            return true; // Empty input is valid
        }
        
        // Pre-process: remove whitespace/newlines and skip old "+ " prefix
        std::string clean;
        clean.reserve(in.size());
        
        size_t start = 0;
        // Remove the questionable "+ " prefix if present
        if (in.size() >= 2 && in[0] == '+' && in[1] == ' ') {
            start = 2;
        }
        
        // Copy non-whitespace characters
        for (size_t i = start; i < in.size(); ++i) {
            char c = in[i];
            if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
                continue; // Skip whitespace
            }
            if (c == '\r') {
                break; // Early termination on \r
            }
            clean += c;
        }
        
        if (clean.empty()) {
            return true; // Only whitespace is valid
        }
        
        // Validate length is multiple of 4
        if (clean.size() % 4 != 0) {
            return false;
        }
        
        out.reserve((clean.size() * 3 / 4) + 1); // Correct calculation
        
        // Process in safe 4-byte chunks
        for (size_t i = 0; i < clean.size(); i += 4) {
            unsigned char digit1 = static_cast<unsigned char>(clean[i]);
            unsigned char digit2 = static_cast<unsigned char>(clean[i+1]);
            unsigned char digit3 = static_cast<unsigned char>(clean[i+2]);
            unsigned char digit4 = static_cast<unsigned char>(clean[i+3]);
            
            // Validate all digits before processing
            if (DECODE64(digit1) == BAD || DECODE64(digit2) == BAD) {
                return false;
            }
            if (digit3 != '=' && DECODE64(digit3) == BAD) {
                return false;
            }
            if (digit4 != '=' && DECODE64(digit4) == BAD) {
                return false;
            }
            
            // Decode the 4-byte group
            out += (DECODE64(digit1) << 2) | (DECODE64(digit2) >> 4);
            if (digit3 != '=') {
                out += ((DECODE64(digit2) << 4) & 0xf0) | (DECODE64(digit3) >> 2);
                if (digit4 != '=') {
                    out += ((DECODE64(digit3) << 6) & 0xc0) | DECODE64(digit4);
                }
            }
            
            // Stop at padding
            if (digit4 == '=' || digit3 == '=') {
                break;
            }
        }
        
        return true;
    }
}
