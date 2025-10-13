/*
 * 32-bit xorwow-based PRNG with a seed input. Based on George Marsaglia's
 * Xorshift RNGs paper.
 *
 * Copyright (c) 2025 Nicholas Clark <nicholas.clark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>

#include "genimage.h"

struct prng_state {
	uint32_t lane[5];
	uint32_t counter;
};

static uint32_t prng_getrandom(struct prng_state *state)
{
	uint32_t buffer = state->lane[4];

	state->lane[4] = state->lane[3];
	state->lane[3] = state->lane[2];
	state->lane[2] = state->lane[1];
	state->lane[1] = state->lane[0];

	buffer ^= buffer >> 2;
	buffer ^= buffer << 1;
	buffer ^= state->lane[0] ^ (state->lane[0] << 4);
	state->lane[0] = buffer;
	state->counter += 0x587C5;

	return buffer + state->counter;
}

static void prng_seed(struct prng_state *state, uint32_t seed)
{
	state->counter = seed;
	state->lane[0] = 1 + (seed & 0x0FFFFFFFu);
	state->lane[1] = 2 + (seed & 0xFF0FFFFFu);
	state->lane[2] = 3 + (seed & 0xFFF00FFFu);
	state->lane[3] = 4 + (seed & 0xFFFFF0FFu);
	state->lane[4] = 5 + (seed & 0xFFFFFFF0u);

	for (int x = 0; x < 8; x++) {
		prng_getrandom(state);
	}
}

static struct prng_state prng = { { 1, 2, 3, 4, 5 }, 0 };
static bool prng_active = false;

void random32_init(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	srandom(tv.tv_usec);
	prng_active = false;
}

void random32_enable_prng(const char *seed, size_t length)
{
	prng_seed(&prng, crc32(seed, length));
	prng_active = true;
}

uint32_t random32(void)
{
	if (prng_active) {
		return prng_getrandom(&prng);
	}
	return random();
}
