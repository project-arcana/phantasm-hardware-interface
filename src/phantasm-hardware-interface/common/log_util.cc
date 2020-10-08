#include "log_util.hh"

#include <cstdio>
#include <cstring>

#include <clean-core/bits.hh>

void phi::log::dump_hex(const void* data, int length)
{
    int i = 0;
    unsigned char buff[17];                                         // stores the ASCII data
    auto const* const pc = static_cast<unsigned char const*>(data); // cast to make the code cleaner.
    buff[16] = '\0';

    // loop body
    // 16 bytes at once

    while (i + 16 < length)
    {
        for (auto j = 0; j < 16; ++j)
        {
            if ((pc[i + j] < 0x20) || (pc[i + j] > 0x7e))
                buff[j] = '.';
            else
                buff[j] = pc[i + j];
        }

        std::printf("  %04x  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x  %s\n", i, //
                    pc[i + 0], pc[i + 1], pc[i + 2], pc[i + 3],                                                         //
                    pc[i + 4], pc[i + 5], pc[i + 6], pc[i + 7],                                                         //
                    pc[i + 8], pc[i + 9], pc[i + 10], pc[i + 11],                                                       //
                    pc[i + 12], pc[i + 13], pc[i + 14], pc[i + 15],                                                     //
                    buff);

        i += 16;
    }

    // loop epilogue

    if (i < length)
    {
        // begin the last line
        std::printf("  %04x ", i);

        auto j = 0;
        while (i < length)
        {
            // fill in the last line
            std::printf(" %02x", pc[i]);
            if ((pc[i] < 0x20) || (pc[i] > 0x7e))
                buff[j] = '.';
            else
                buff[j] = pc[i];
            ++i;
            ++j;
        }

        while (j < 16)
        {
            buff[j] = ' ';
            ++j;
        }

        while (cc::mod_pow2(i, 16u) != 0)
        {
            // pad out the last line
            std::printf("   ");
            ++i;
        }

        // print the last data-as-string
        std::printf("  %s\n", buff);
    }
}
