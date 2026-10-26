#pragma once
#include <nds/ndstypes.h>

extern "C" {
extern const u8 song_bin[];
extern const u8 song_bin_end[];
}

#define song_bin_size ((u32)(song_bin_end-song_bin))
