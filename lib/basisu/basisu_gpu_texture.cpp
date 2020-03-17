// basisu_gpu_texture.cpp
// Copyright (C) 2019 Binomial LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "basisu_gpu_texture.h"
#include "basisu_enc.h"
#include "basisu_pvrtc1_4.h"
#include "basisu_astc_decomp.h"

namespace basisu
{
	const int8_t g_etc2_eac_tables[16][8] = 
	{
		{ -3, -6, -9, -15, 2, 5, 8, 14 }, { -3, -7, -10, -13, 2, 6, 9, 12 }, { -2, -5, -8, -13, 1, 4, 7, 12 }, { -2, -4, -6, -13, 1, 3, 5, 12 },
		{ -3, -6, -8, -12, 2, 5, 7, 11 }, { -3, -7, -9, -11, 2, 6, 8, 10 }, { -4, -7, -8, -11, 3, 6, 7, 10 }, { -3, -5, -8, -11, 2, 4, 7, 10 },
		{ -2, -6, -8, -10, 1, 5, 7, 9 }, { -2, -5, -8, -10, 1, 4, 7, 9 }, { -2, -4, -8, -10, 1, 3, 7, 9 }, { -2, -5, -7, -10, 1, 4, 6, 9 },
		{ -3, -4, -7, -10, 2, 3, 6, 9 }, { -1, -2, -3, -10, 0, 1, 2, 9 }, { -4, -6, -8, -9, 3, 5, 7, 8 }, { -3, -5, -7, -9, 2, 4, 6, 8 }
	};

	struct eac_a8_block
	{
		uint16_t m_base : 8;
		uint16_t m_table : 4;
		uint16_t m_multiplier : 4;

		uint8_t m_selectors[6];

		inline uint32_t get_selector(uint32_t x, uint32_t y, uint64_t selector_bits) const
		{
			assert((x < 4) && (y < 4));
			return static_cast<uint32_t>((selector_bits >> (45 - (y + x * 4) * 3)) & 7);
		}
				
		inline uint64_t get_selector_bits() const
		{
			uint64_t pixels = ((uint64_t)m_selectors[0] << 40) | ((uint64_t)m_selectors[1] << 32) | ((uint64_t)m_selectors[2] << 24) |	((uint64_t)m_selectors[3] << 16) | ((uint64_t)m_selectors[4] << 8) | m_selectors[5];
			return pixels;
		}
	};
		
	void unpack_etc2_eac(const void *pBlock_bits, color_rgba *pPixels)
	{
		static_assert(sizeof(eac_a8_block) == 8, "sizeof(eac_a8_block) == 8");

		const eac_a8_block *pBlock = static_cast<const eac_a8_block *>(pBlock_bits);

		const int8_t *pTable = g_etc2_eac_tables[pBlock->m_table];
		
		const uint64_t selector_bits = pBlock->get_selector_bits();
		
		const int32_t base = pBlock->m_base;
		const int32_t mul = pBlock->m_multiplier;

		pPixels[0].a = clamp255(base + pTable[pBlock->get_selector(0, 0, selector_bits)] * mul);
		pPixels[1].a = clamp255(base + pTable[pBlock->get_selector(1, 0, selector_bits)] * mul);
		pPixels[2].a = clamp255(base + pTable[pBlock->get_selector(2, 0, selector_bits)] * mul);
		pPixels[3].a = clamp255(base + pTable[pBlock->get_selector(3, 0, selector_bits)] * mul);

		pPixels[4].a = clamp255(base + pTable[pBlock->get_selector(0, 1, selector_bits)] * mul);
		pPixels[5].a = clamp255(base + pTable[pBlock->get_selector(1, 1, selector_bits)] * mul);
		pPixels[6].a = clamp255(base + pTable[pBlock->get_selector(2, 1, selector_bits)] * mul);
		pPixels[7].a = clamp255(base + pTable[pBlock->get_selector(3, 1, selector_bits)] * mul);

		pPixels[8].a = clamp255(base + pTable[pBlock->get_selector(0, 2, selector_bits)] * mul);
		pPixels[9].a = clamp255(base + pTable[pBlock->get_selector(1, 2, selector_bits)] * mul);
		pPixels[10].a = clamp255(base + pTable[pBlock->get_selector(2, 2, selector_bits)] * mul);
		pPixels[11].a = clamp255(base + pTable[pBlock->get_selector(3, 2, selector_bits)] * mul);

		pPixels[12].a = clamp255(base + pTable[pBlock->get_selector(0, 3, selector_bits)] * mul);
		pPixels[13].a = clamp255(base + pTable[pBlock->get_selector(1, 3, selector_bits)] * mul);
		pPixels[14].a = clamp255(base + pTable[pBlock->get_selector(2, 3, selector_bits)] * mul);
		pPixels[15].a = clamp255(base + pTable[pBlock->get_selector(3, 3, selector_bits)] * mul);
	}

	struct bc1_block
	{
		enum { cTotalEndpointBytes = 2, cTotalSelectorBytes = 4 };

		uint8_t m_low_color[cTotalEndpointBytes];
		uint8_t m_high_color[cTotalEndpointBytes];
		uint8_t m_selectors[cTotalSelectorBytes];
				
		inline uint32_t get_high_color() const	{ return m_high_color[0] | (m_high_color[1] << 8U); }
		inline uint32_t get_low_color() const { return m_low_color[0] | (m_low_color[1] << 8U); }

		static void unpack_color(uint32_t c, uint32_t &r, uint32_t &g, uint32_t &b) 
		{
			r = (c >> 11) & 31;
			g = (c >> 5) & 63;
			b = c & 31;
			
			r = (r << 3) | (r >> 2);
			g = (g << 2) | (g >> 4);
			b = (b << 3) | (b >> 2);
		}

		inline uint32_t get_selector(uint32_t x, uint32_t y) const { assert((x < 4U) && (y < 4U)); return (m_selectors[y] >> (x * 2)) & 3; }
	};

	// Returns true if the block uses 3 color punchthrough alpha mode.
	bool unpack_bc1(const void *pBlock_bits, color_rgba *pPixels, bool set_alpha)
	{
		static_assert(sizeof(bc1_block) == 8, "sizeof(bc1_block) == 8");

		const bc1_block *pBlock = static_cast<const bc1_block *>(pBlock_bits);

		const uint32_t l = pBlock->get_low_color();
		const uint32_t h = pBlock->get_high_color();

		color_rgba c[4];

		uint32_t r0, g0, b0, r1, g1, b1;
		bc1_block::unpack_color(l, r0, g0, b0);
		bc1_block::unpack_color(h, r1, g1, b1);

		bool used_punchthrough = false;

		if (l > h)
		{
			c[0].set_noclamp_rgba(r0, g0, b0, 255);
			c[1].set_noclamp_rgba(r1, g1, b1, 255);
			c[2].set_noclamp_rgba((r0 * 2 + r1) / 3, (g0 * 2 + g1) / 3, (b0 * 2 + b1) / 3, 255);
			c[3].set_noclamp_rgba((r1 * 2 + r0) / 3, (g1 * 2 + g0) / 3, (b1 * 2 + b0) / 3, 255);
		}
		else
		{
			c[0].set_noclamp_rgba(r0, g0, b0, 255);
			c[1].set_noclamp_rgba(r1, g1, b1, 255);
			c[2].set_noclamp_rgba((r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255);
			c[3].set_noclamp_rgba(0, 0, 0, 0);
			used_punchthrough = true;
		}

		if (set_alpha)
		{
			for (uint32_t y = 0; y < 4; y++, pPixels += 4)
			{
				pPixels[0] = c[pBlock->get_selector(0, y)]; 
				pPixels[1] = c[pBlock->get_selector(1, y)]; 
				pPixels[2] = c[pBlock->get_selector(2, y)]; 
				pPixels[3] = c[pBlock->get_selector(3, y)];
			}
		}
		else
		{
			for (uint32_t y = 0; y < 4; y++, pPixels += 4)
			{
				pPixels[0].set_rgb(c[pBlock->get_selector(0, y)]); 
				pPixels[1].set_rgb(c[pBlock->get_selector(1, y)]); 
				pPixels[2].set_rgb(c[pBlock->get_selector(2, y)]); 
				pPixels[3].set_rgb(c[pBlock->get_selector(3, y)]);
			}
		}

		return used_punchthrough;
	}

	struct bc4_block
	{
		enum { cBC4SelectorBits = 3, cTotalSelectorBytes = 6, cMaxSelectorValues = 8 };
		uint8_t m_endpoints[2];

		uint8_t m_selectors[cTotalSelectorBytes];

		inline uint32_t get_low_alpha() const { return m_endpoints[0]; }
		inline uint32_t get_high_alpha() const { return m_endpoints[1]; }
		inline bool is_alpha6_block() const { return get_low_alpha() <= get_high_alpha(); }

		inline uint64_t get_selector_bits() const
		{ 
			return ((uint64_t)((uint32_t)m_selectors[0] | ((uint32_t)m_selectors[1] << 8U) | ((uint32_t)m_selectors[2] << 16U) | ((uint32_t)m_selectors[3] << 24U))) |
				(((uint64_t)m_selectors[4]) << 32U) |
				(((uint64_t)m_selectors[5]) << 40U);
		}

		inline uint32_t get_selector(uint32_t x, uint32_t y, uint64_t selector_bits) const
		{
			assert((x < 4U) && (y < 4U));
			return (selector_bits >> (((y * 4) + x) * cBC4SelectorBits)) & (cMaxSelectorValues - 1);
		}
				
		static inline uint32_t get_block_values6(uint8_t *pDst, uint32_t l, uint32_t h)
		{
			pDst[0] = static_cast<uint8_t>(l);
			pDst[1] = static_cast<uint8_t>(h);
			pDst[2] = static_cast<uint8_t>((l * 4 + h) / 5);
			pDst[3] = static_cast<uint8_t>((l * 3 + h * 2) / 5);
			pDst[4] = static_cast<uint8_t>((l * 2 + h * 3) / 5);
			pDst[5] = static_cast<uint8_t>((l + h * 4) / 5);
			pDst[6] = 0;
			pDst[7] = 255;
			return 6;
		}

		static inline uint32_t get_block_values8(uint8_t *pDst, uint32_t l, uint32_t h)
		{
			pDst[0] = static_cast<uint8_t>(l);
			pDst[1] = static_cast<uint8_t>(h);
			pDst[2] = static_cast<uint8_t>((l * 6 + h) / 7);
			pDst[3] = static_cast<uint8_t>((l * 5 + h * 2) / 7);
			pDst[4] = static_cast<uint8_t>((l * 4 + h * 3) / 7);
			pDst[5] = static_cast<uint8_t>((l * 3 + h * 4) / 7);
			pDst[6] = static_cast<uint8_t>((l * 2 + h * 5) / 7);
			pDst[7] = static_cast<uint8_t>((l + h * 6) / 7);
			return 8;
		}

		static inline uint32_t get_block_values(uint8_t *pDst, uint32_t l, uint32_t h)
		{
			if (l > h)
				return get_block_values8(pDst, l, h);
			else
				return get_block_values6(pDst, l, h);
		}
	};

	void unpack_bc4(const void *pBlock_bits, uint8_t *pPixels, uint32_t stride)
	{
		static_assert(sizeof(bc4_block) == 8, "sizeof(bc4_block) == 8");

		const bc4_block *pBlock = static_cast<const bc4_block *>(pBlock_bits);

		uint8_t sel_values[8];
		bc4_block::get_block_values(sel_values, pBlock->get_low_alpha(), pBlock->get_high_alpha());

		const uint64_t selector_bits = pBlock->get_selector_bits();

		for (uint32_t y = 0; y < 4; y++, pPixels += (stride * 4U))
		{
			pPixels[0] = sel_values[pBlock->get_selector(0, y, selector_bits)];
			pPixels[stride * 1] = sel_values[pBlock->get_selector(1, y, selector_bits)];
			pPixels[stride * 2] = sel_values[pBlock->get_selector(2, y, selector_bits)];
			pPixels[stride * 3] = sel_values[pBlock->get_selector(3, y, selector_bits)];
		}
	}
	
	// Returns false if the block uses 3-color punchthrough alpha mode, which isn't supported on some GPU's for BC3.
	bool unpack_bc3(const void *pBlock_bits, color_rgba *pPixels)
	{
		bool success = true;

		if (unpack_bc1((const uint8_t *)pBlock_bits + sizeof(bc4_block), pPixels, true))
			success = false;

		unpack_bc4(pBlock_bits, &pPixels[0].a, sizeof(color_rgba));
		
		return success;
	}

	// writes RG
	void unpack_bc5(const void *pBlock_bits, color_rgba *pPixels)
	{
		unpack_bc4(pBlock_bits, &pPixels[0].r, sizeof(color_rgba));
		unpack_bc4((const uint8_t *)pBlock_bits + sizeof(bc4_block), &pPixels[0].g, sizeof(color_rgba));
	}

	// ATC isn't officially documented, so I'm assuming these references:
	// http://www.guildsoftware.com/papers/2012.Converting.DXTC.to.ATC.pdf
	// https://github.com/Triang3l/S3TConv/blob/master/s3tconv_atitc.c
	// The paper incorrectly says the ATC lerp factors are 1/3 and 2/3, but they are actually 3/8 and 5/8.
	void unpack_atc(const void* pBlock_bits, color_rgba* pPixels)
	{
		const uint8_t* pBytes = static_cast<const uint8_t*>(pBlock_bits);

		const uint16_t color0 = pBytes[0] | (pBytes[1] << 8U);
		const uint16_t color1 = pBytes[2] | (pBytes[3] << 8U);
		uint32_t sels = pBytes[4] | (pBytes[5] << 8U) | (pBytes[6] << 16U) | (pBytes[7] << 24U);

		const bool mode = (color0 & 0x8000) != 0;

		color_rgba c[4];

		c[0].set((color0 >> 10) & 31, (color0 >> 5) & 31, color0 & 31, 255);
		c[0].r = (c[0].r << 3) | (c[0].r >> 2);
		c[0].g = (c[0].g << 3) | (c[0].g >> 2);
		c[0].b = (c[0].b << 3) | (c[0].b >> 2);

		c[3].set((color1 >> 11) & 31, (color1 >> 5) & 63, color1 & 31, 255);
		c[3].r = (c[3].r << 3) | (c[3].r >> 2);
		c[3].g = (c[3].g << 2) | (c[3].g >> 4);
		c[3].b = (c[3].b << 3) | (c[3].b >> 2);

		if (mode)
		{
			c[1].set(std::max(0, c[0].r - (c[3].r >> 2)), std::max(0, c[0].g - (c[3].g >> 2)), std::max(0, c[0].b - (c[3].b >> 2)), 255);
			c[2] = c[0];
			c[0].set(0, 0, 0, 255);
		}
		else
		{
			c[1].r = (c[0].r * 5 + c[3].r * 3) >> 3;
			c[1].g = (c[0].g * 5 + c[3].g * 3) >> 3;
			c[1].b = (c[0].b * 5 + c[3].b * 3) >> 3;

			c[2].r = (c[0].r * 3 + c[3].r * 5) >> 3;
			c[2].g = (c[0].g * 3 + c[3].g * 5) >> 3;
			c[2].b = (c[0].b * 3 + c[3].b * 5) >> 3;
		}

		for (uint32_t i = 0; i < 16; i++)
		{
			const uint32_t s = sels & 3;
			
			pPixels[i] = c[s];
							
			sels >>= 2;
		}
	}

	struct bc7_mode_6
	{
		struct
		{
			uint64_t m_mode : 7;
			uint64_t m_r0 : 7;
			uint64_t m_r1 : 7;
			uint64_t m_g0 : 7;
			uint64_t m_g1 : 7;
			uint64_t m_b0 : 7;
			uint64_t m_b1 : 7;
			uint64_t m_a0 : 7;
			uint64_t m_a1 : 7;
			uint64_t m_p0 : 1;
		} m_lo;

		union
		{
			struct
			{
				uint64_t m_p1 : 1;
				uint64_t m_s00 : 3;
				uint64_t m_s10 : 4;
				uint64_t m_s20 : 4;
				uint64_t m_s30 : 4;

				uint64_t m_s01 : 4;
				uint64_t m_s11 : 4;
				uint64_t m_s21 : 4;
				uint64_t m_s31 : 4;

				uint64_t m_s02 : 4;
				uint64_t m_s12 : 4;
				uint64_t m_s22 : 4;
				uint64_t m_s32 : 4;

				uint64_t m_s03 : 4;
				uint64_t m_s13 : 4;
				uint64_t m_s23 : 4;
				uint64_t m_s33 : 4;

			} m_hi;

			uint64_t m_hi_bits;
		};
	};

	static const uint32_t g_bc7_weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
	
	// The transcoder only outputs mode 6 at the moment, so this is easy.
	bool unpack_bc7_mode6(const void *pBlock_bits, color_rgba *pPixels)
	{
		static_assert(sizeof(bc7_mode_6) == 16, "sizeof(bc7_mode_6) == 16");

		const bc7_mode_6 &block = *static_cast<const bc7_mode_6 *>(pBlock_bits);

		if (block.m_lo.m_mode != (1 << 6))
			return false;

		const uint32_t r0 = (uint32_t)((block.m_lo.m_r0 << 1) | block.m_lo.m_p0);
		const uint32_t g0 = (uint32_t)((block.m_lo.m_g0 << 1) | block.m_lo.m_p0);
		const uint32_t b0 = (uint32_t)((block.m_lo.m_b0 << 1) | block.m_lo.m_p0);
		const uint32_t a0 = (uint32_t)((block.m_lo.m_a0 << 1) | block.m_lo.m_p0);
		const uint32_t r1 = (uint32_t)((block.m_lo.m_r1 << 1) | block.m_hi.m_p1);
		const uint32_t g1 = (uint32_t)((block.m_lo.m_g1 << 1) | block.m_hi.m_p1);
		const uint32_t b1 = (uint32_t)((block.m_lo.m_b1 << 1) | block.m_hi.m_p1);
		const uint32_t a1 = (uint32_t)((block.m_lo.m_a1 << 1) | block.m_hi.m_p1);

		color_rgba vals[16];
		for (uint32_t i = 0; i < 16; i++)
		{
			const uint32_t w = g_bc7_weights4[i];
			const uint32_t iw = 64 - w;
			vals[i].set_noclamp_rgba( 
				(r0 * iw + r1 * w + 32) >> 6, 
				(g0 * iw + g1 * w + 32) >> 6, 
				(b0 * iw + b1 * w + 32) >> 6, 
				(a0 * iw + a1 * w + 32) >> 6);
		}

		pPixels[0] = vals[block.m_hi.m_s00];
		pPixels[1] = vals[block.m_hi.m_s10];
		pPixels[2] = vals[block.m_hi.m_s20];
		pPixels[3] = vals[block.m_hi.m_s30];

		pPixels[4] = vals[block.m_hi.m_s01];
		pPixels[5] = vals[block.m_hi.m_s11];
		pPixels[6] = vals[block.m_hi.m_s21];
		pPixels[7] = vals[block.m_hi.m_s31];
		
		pPixels[8] = vals[block.m_hi.m_s02];
		pPixels[9] = vals[block.m_hi.m_s12];
		pPixels[10] = vals[block.m_hi.m_s22];
		pPixels[11] = vals[block.m_hi.m_s32];

		pPixels[12] = vals[block.m_hi.m_s03];
		pPixels[13] = vals[block.m_hi.m_s13];
		pPixels[14] = vals[block.m_hi.m_s23];
		pPixels[15] = vals[block.m_hi.m_s33];

		return true;
	}

	static inline uint32_t get_block_bits(const uint8_t* pBytes, uint32_t bit_ofs, uint32_t bits_wanted)
	{
		assert(bits_wanted < 32);

		uint32_t v = 0;
		uint32_t total_bits = 0;

		while (total_bits < bits_wanted)
		{
			uint32_t k = pBytes[bit_ofs >> 3];
			k >>= (bit_ofs & 7);
			uint32_t num_bits_in_byte = 8 - (bit_ofs & 7);

			v |= (k << total_bits);
			total_bits += num_bits_in_byte;
			bit_ofs += num_bits_in_byte;
		}

		return v & ((1 << bits_wanted) - 1);
	}
						
	struct bc7_mode_5
	{
		union
		{
			struct
			{
				uint64_t m_mode : 6;
				uint64_t m_rot : 2;
				
				uint64_t m_r0 : 7;
				uint64_t m_r1 : 7;
				uint64_t m_g0 : 7;
				uint64_t m_g1 : 7;
				uint64_t m_b0 : 7;
				uint64_t m_b1 : 7;
				uint64_t m_a0 : 8;
				uint64_t m_a1_0 : 6;

			} m_lo;

			uint64_t m_lo_bits;
		};

		union
		{
			struct
			{
				uint64_t m_a1_1 : 2;

				// bit 2
				uint64_t m_c00 : 1;
				uint64_t m_c10 : 2;
				uint64_t m_c20 : 2;
				uint64_t m_c30 : 2;

				uint64_t m_c01 : 2;
				uint64_t m_c11 : 2;
				uint64_t m_c21 : 2;
				uint64_t m_c31 : 2;

				uint64_t m_c02 : 2;
				uint64_t m_c12 : 2;
				uint64_t m_c22 : 2;
				uint64_t m_c32 : 2;

				uint64_t m_c03 : 2;
				uint64_t m_c13 : 2;
				uint64_t m_c23 : 2;
				uint64_t m_c33 : 2;

				// bit 33
				uint64_t m_a00 : 1;
				uint64_t m_a10 : 2;
				uint64_t m_a20 : 2;
				uint64_t m_a30 : 2;

				uint64_t m_a01 : 2;
				uint64_t m_a11 : 2;
				uint64_t m_a21 : 2;
				uint64_t m_a31 : 2;

				uint64_t m_a02 : 2;
				uint64_t m_a12 : 2;
				uint64_t m_a22 : 2;
				uint64_t m_a32 : 2;

				uint64_t m_a03 : 2;
				uint64_t m_a13 : 2;
				uint64_t m_a23 : 2;
				uint64_t m_a33 : 2;

			} m_hi;

			uint64_t m_hi_bits;
		};

		color_rgba get_low_color() const
		{
			return color_rgba(cNoClamp,
				(int)((m_lo.m_r0 << 1) | (m_lo.m_r0 >> 6)),
				(int)((m_lo.m_g0 << 1) | (m_lo.m_g0 >> 6)),
				(int)((m_lo.m_b0 << 1) | (m_lo.m_b0 >> 6)),
				m_lo.m_a0);
		}

		color_rgba get_high_color() const
		{
			return color_rgba(cNoClamp,
				(int)((m_lo.m_r1 << 1) | (m_lo.m_r1 >> 6)),
				(int)((m_lo.m_g1 << 1) | (m_lo.m_g1 >> 6)),
				(int)((m_lo.m_b1 << 1) | (m_lo.m_b1 >> 6)),
				(int)m_lo.m_a1_0 | ((int)m_hi.m_a1_1 << 6));
		}

		void get_block_colors(color_rgba* pColors) const
		{
			const color_rgba low_color(get_low_color());
			const color_rgba high_color(get_high_color());

			for (uint32_t i = 0; i < 4; i++)
			{
				static const uint32_t s_bc7_weights2[4] = { 0, 21, 43, 64 };

				pColors[i].set_noclamp_rgba(
					(low_color.r * (64 - s_bc7_weights2[i]) + high_color.r * s_bc7_weights2[i] + 32) >> 6,
					(low_color.g * (64 - s_bc7_weights2[i]) + high_color.g * s_bc7_weights2[i] + 32) >> 6,
					(low_color.b * (64 - s_bc7_weights2[i]) + high_color.b * s_bc7_weights2[i] + 32) >> 6,
					(low_color.a * (64 - s_bc7_weights2[i]) + high_color.a * s_bc7_weights2[i] + 32) >> 6);
			}
		} 

		uint32_t get_selector(uint32_t idx, bool alpha) const
		{
			const uint32_t size = (idx == 0) ? 1 : 2;

			uint32_t ofs = alpha ? 97 : 66;
			
			if (idx)
				ofs += 1 + 2 * (idx - 1);

			return get_block_bits(reinterpret_cast<const uint8_t*>(this), ofs, size);
		}
	};

	bool unpack_bc7_mode5(const void* pBlock_bits, color_rgba* pPixels)
	{
		static_assert(sizeof(bc7_mode_5) == 16, "sizeof(bc7_mode_5) == 16");

		const bc7_mode_5& block = *static_cast<const bc7_mode_5*>(pBlock_bits);

		if (block.m_lo.m_mode != (1 << 5))
			return false;
				
		color_rgba block_colors[4];
		block.get_block_colors(block_colors);

		const uint32_t rot = block.m_lo.m_rot;

		for (uint32_t i = 0; i < 16; i++)
		{
			const uint32_t cs = block.get_selector(i, false);

			color_rgba c(block_colors[cs]);

			const uint32_t as = block.get_selector(i, true);
			c.a = block_colors[as].a;

			if (rot > 0)
				std::swap(c[3], c[rot - 1]);

			pPixels[i] = c;
		}

		return true;
	}

	struct fxt1_block
	{
		union
		{
			struct
			{
				uint64_t m_t00 : 2;
				uint64_t m_t01 : 2;
				uint64_t m_t02 : 2;
				uint64_t m_t03 : 2;
				uint64_t m_t04 : 2;
				uint64_t m_t05 : 2;
				uint64_t m_t06 : 2;
				uint64_t m_t07 : 2;
				uint64_t m_t08 : 2;
				uint64_t m_t09 : 2;
				uint64_t m_t10 : 2;
				uint64_t m_t11 : 2;
				uint64_t m_t12 : 2;
				uint64_t m_t13 : 2;
				uint64_t m_t14 : 2;
				uint64_t m_t15 : 2;
				uint64_t m_t16 : 2;
				uint64_t m_t17 : 2;
				uint64_t m_t18 : 2;
				uint64_t m_t19 : 2;
				uint64_t m_t20 : 2;
				uint64_t m_t21 : 2;
				uint64_t m_t22 : 2;
				uint64_t m_t23 : 2;
				uint64_t m_t24 : 2;
				uint64_t m_t25 : 2;
				uint64_t m_t26 : 2;
				uint64_t m_t27 : 2;
				uint64_t m_t28 : 2;
				uint64_t m_t29 : 2;
				uint64_t m_t30 : 2;
				uint64_t m_t31 : 2;
			} m_lo;
			uint64_t m_lo_bits;
			uint8_t m_sels[8];
		};

		union
		{
			struct
			{
#ifdef BASISU_USE_ORIGINAL_3DFX_FXT1_ENCODING
				// This is the format that 3DFX's DECOMP.EXE tool expects, which I'm assuming is what the actual 3DFX hardware wanted.
				// Unfortunately, color0/color1 and color2/color3 are flipped relative to the official OpenGL extension and Intel's documentation!
				uint64_t m_b1 : 5;
				uint64_t m_g1 : 5;
				uint64_t m_r1 : 5;
				uint64_t m_b0 : 5;
				uint64_t m_g0 : 5;
				uint64_t m_r0 : 5;
				uint64_t m_b3 : 5;
				uint64_t m_g3 : 5;
				uint64_t m_r3 : 5;
				uint64_t m_b2 : 5;
				uint64_t m_g2 : 5;
				uint64_t m_r2 : 5;
#else
				// Intel's encoding, and the encoding in the OpenGL FXT1 spec.
				uint64_t m_b0 : 5;
				uint64_t m_g0 : 5;
				uint64_t m_r0 : 5;
				uint64_t m_b1 : 5;
				uint64_t m_g1 : 5;
				uint64_t m_r1 : 5;
				uint64_t m_b2 : 5;
				uint64_t m_g2 : 5;
				uint64_t m_r2 : 5;
				uint64_t m_b3 : 5;
				uint64_t m_g3 : 5;
				uint64_t m_r3 : 5;
#endif
				uint64_t m_alpha : 1;
				uint64_t m_glsb : 2;
				uint64_t m_mode : 1;
			} m_hi;

			uint64_t m_hi_bits;
		};
	};

	static color_rgba expand_565(const color_rgba& c)
	{
		return color_rgba((c.r << 3) | (c.r >> 2), (c.g << 2) | (c.g >> 4), (c.b << 3) | (c.b >> 2), 255);
	}

	// We only support CC_MIXED non-alpha blocks here because that's the only mode the transcoder uses at the moment.
	bool unpack_fxt1(const void *p, color_rgba *pPixels)
	{
		const fxt1_block* pBlock = static_cast<const fxt1_block*>(p);

		if (pBlock->m_hi.m_mode == 0)
			return false;
		if (pBlock->m_hi.m_alpha == 1)
			return false;
				
		color_rgba colors[4];

		colors[0].r = pBlock->m_hi.m_r0;
		colors[0].g = (uint8_t)((pBlock->m_hi.m_g0 << 1) | ((pBlock->m_lo.m_t00 >> 1) ^ (pBlock->m_hi.m_glsb & 1)));
		colors[0].b = pBlock->m_hi.m_b0;
		colors[0].a = 255;

		colors[1].r = pBlock->m_hi.m_r1;
		colors[1].g = (uint8_t)((pBlock->m_hi.m_g1 << 1) | (pBlock->m_hi.m_glsb & 1));
		colors[1].b = pBlock->m_hi.m_b1;
		colors[1].a = 255;

		colors[2].r = pBlock->m_hi.m_r2;
		colors[2].g = (uint8_t)((pBlock->m_hi.m_g2 << 1) | ((pBlock->m_lo.m_t16 >> 1) ^ (pBlock->m_hi.m_glsb >> 1)));
		colors[2].b = pBlock->m_hi.m_b2;
		colors[2].a = 255;

		colors[3].r = pBlock->m_hi.m_r3;
		colors[3].g = (uint8_t)((pBlock->m_hi.m_g3 << 1) | (pBlock->m_hi.m_glsb >> 1));
		colors[3].b = pBlock->m_hi.m_b3;
		colors[3].a = 255;

		for (uint32_t i = 0; i < 4; i++)
			colors[i] = expand_565(colors[i]);

		color_rgba block0_colors[4];
		block0_colors[0] = colors[0];
		block0_colors[1] = color_rgba((colors[0].r * 2 + colors[1].r + 1) / 3, (colors[0].g * 2 + colors[1].g + 1) / 3, (colors[0].b * 2 + colors[1].b + 1) / 3, 255);
		block0_colors[2] = color_rgba((colors[1].r * 2 + colors[0].r + 1) / 3, (colors[1].g * 2 + colors[0].g + 1) / 3, (colors[1].b * 2 + colors[0].b + 1) / 3, 255);
		block0_colors[3] = colors[1];

		for (uint32_t i = 0; i < 16; i++)
		{
			const uint32_t sel = (pBlock->m_sels[i >> 2] >> ((i & 3) * 2)) & 3;

			const uint32_t x = i & 3;
			const uint32_t y = i >> 2;
			pPixels[x + y * 8] = block0_colors[sel];
		}

		color_rgba block1_colors[4];
		block1_colors[0] = colors[2];
		block1_colors[1] = color_rgba((colors[2].r * 2 + colors[3].r + 1) / 3, (colors[2].g * 2 + colors[3].g + 1) / 3, (colors[2].b * 2 + colors[3].b + 1) / 3, 255);
		block1_colors[2] = color_rgba((colors[3].r * 2 + colors[2].r + 1) / 3, (colors[3].g * 2 + colors[2].g + 1) / 3, (colors[3].b * 2 + colors[2].b + 1) / 3, 255);
		block1_colors[3] = colors[3];

		for (uint32_t i = 0; i < 16; i++)
		{
			const uint32_t sel = (pBlock->m_sels[4 + (i >> 2)] >> ((i & 3) * 2)) & 3;
			
			const uint32_t x = i & 3;
			const uint32_t y = i >> 2;
			pPixels[4 + x + y * 8] = block1_colors[sel];
		}

		return true;
	}

	struct pvrtc2_block
	{
		uint8_t m_modulation[4];

		union
		{
			union
			{
				// Opaque mode: RGB colora=554 and colorb=555
				struct
				{
					uint32_t m_mod_flag : 1;
					uint32_t m_blue_a : 4;
					uint32_t m_green_a : 5;
					uint32_t m_red_a : 5;
					uint32_t m_hard_flag : 1;
					uint32_t m_blue_b : 5;
					uint32_t m_green_b : 5;
					uint32_t m_red_b : 5;
					uint32_t m_opaque_flag : 1;

				} m_opaque_color_data;

				// Transparent mode: RGBA colora=4433 and colorb=4443
				struct
				{
					uint32_t m_mod_flag : 1;
					uint32_t m_blue_a : 3;
					uint32_t m_green_a : 4;
					uint32_t m_red_a : 4;
					uint32_t m_alpha_a : 3;
					uint32_t m_hard_flag : 1;
					uint32_t m_blue_b : 4;
					uint32_t m_green_b : 4;
					uint32_t m_red_b : 4;
					uint32_t m_alpha_b : 3;
					uint32_t m_opaque_flag : 1;

				} m_trans_color_data;
			};

			uint32_t m_color_data_bits;
		};
	};

	static color_rgba convert_rgb_555_to_888(const color_rgba& col)
	{
		return color_rgba((col[0] << 3) | (col[0] >> 2), (col[1] << 3) | (col[1] >> 2), (col[2] << 3) | (col[2] >> 2), 255);
	}
	
	static color_rgba convert_rgba_5554_to_8888(const color_rgba& col)
	{
		return color_rgba((col[0] << 3) | (col[0] >> 2), (col[1] << 3) | (col[1] >> 2), (col[2] << 3) | (col[2] >> 2), (col[3] << 4) | col[3]);
	}

	// PVRTC2 is currently limited to only what our transcoder outputs (non-interpolated, hard_flag=1 modulation=0). In this mode, PVRTC2 looks much like BC1/ATC.
	bool unpack_pvrtc2(const void *p, color_rgba *pPixels)
	{
		const pvrtc2_block* pBlock = static_cast<const pvrtc2_block*>(p);

		if ((!pBlock->m_opaque_color_data.m_hard_flag) || (pBlock->m_opaque_color_data.m_mod_flag))
		{
			// This mode isn't supported by the transcoder, so we aren't bothering with it here.
			return false;
		}

		color_rgba colors[4];

		if (pBlock->m_opaque_color_data.m_opaque_flag)
		{
			// colora=554
			color_rgba color_a(pBlock->m_opaque_color_data.m_red_a, pBlock->m_opaque_color_data.m_green_a, (pBlock->m_opaque_color_data.m_blue_a << 1) | (pBlock->m_opaque_color_data.m_blue_a >> 3), 255);
			
			// colora=555
			color_rgba color_b(pBlock->m_opaque_color_data.m_red_b, pBlock->m_opaque_color_data.m_green_b, pBlock->m_opaque_color_data.m_blue_b, 255);
						
			colors[0] = convert_rgb_555_to_888(color_a);
			colors[3] = convert_rgb_555_to_888(color_b);

			colors[1].set((colors[0].r * 5 + colors[3].r * 3) / 8, (colors[0].g * 5 + colors[3].g * 3) / 8, (colors[0].b * 5 + colors[3].b * 3) / 8, 255);
			colors[2].set((colors[0].r * 3 + colors[3].r * 5) / 8, (colors[0].g * 3 + colors[3].g * 5) / 8, (colors[0].b * 3 + colors[3].b * 5) / 8, 255);
		}
		else
		{
			// colora=4433 
			color_rgba color_a(
				(pBlock->m_trans_color_data.m_red_a << 1) | (pBlock->m_trans_color_data.m_red_a >> 3), 
				(pBlock->m_trans_color_data.m_green_a << 1) | (pBlock->m_trans_color_data.m_green_a >> 3),
				(pBlock->m_trans_color_data.m_blue_a << 2) | (pBlock->m_trans_color_data.m_blue_a >> 1), 
				pBlock->m_trans_color_data.m_alpha_a << 1);

			//colorb=4443
			color_rgba color_b(
				(pBlock->m_trans_color_data.m_red_b << 1) | (pBlock->m_trans_color_data.m_red_b >> 3),
				(pBlock->m_trans_color_data.m_green_b << 1) | (pBlock->m_trans_color_data.m_green_b >> 3),
				(pBlock->m_trans_color_data.m_blue_b << 1) | (pBlock->m_trans_color_data.m_blue_b >> 3),
				(pBlock->m_trans_color_data.m_alpha_b << 1) | 1);

			colors[0] = convert_rgba_5554_to_8888(color_a);
			colors[3] = convert_rgba_5554_to_8888(color_b);
		}

		colors[1].set((colors[0].r * 5 + colors[3].r * 3) / 8, (colors[0].g * 5 + colors[3].g * 3) / 8, (colors[0].b * 5 + colors[3].b * 3) / 8, (colors[0].a * 5 + colors[3].a * 3) / 8);
		colors[2].set((colors[0].r * 3 + colors[3].r * 5) / 8, (colors[0].g * 3 + colors[3].g * 5) / 8, (colors[0].b * 3 + colors[3].b * 5) / 8, (colors[0].a * 3 + colors[3].a * 5) / 8);

		for (uint32_t i = 0; i < 16; i++)
		{
			const uint32_t sel = (pBlock->m_modulation[i >> 2] >> ((i & 3) * 2)) & 3;
			pPixels[i] = colors[sel];
		}

		return true;
	}

	struct etc2_eac_r11
	{
		uint64_t m_base	: 8;
		uint64_t m_table	: 4;
		uint64_t m_mul		: 4;
		uint64_t m_sels_0 : 8;
		uint64_t m_sels_1 : 8;
		uint64_t m_sels_2 : 8;
		uint64_t m_sels_3 : 8;
		uint64_t m_sels_4 : 8;
		uint64_t m_sels_5 : 8;

		uint64_t get_sels() const
		{
			return ((uint64_t)m_sels_0 << 40U) | ((uint64_t)m_sels_1 << 32U) | ((uint64_t)m_sels_2 << 24U) | ((uint64_t)m_sels_3 << 16U) | ((uint64_t)m_sels_4 << 8U) | m_sels_5;
		}

		void set_sels(uint64_t v)
		{
			m_sels_0 = (v >> 40U) & 0xFF;
			m_sels_1 = (v >> 32U) & 0xFF;
			m_sels_2 = (v >> 24U) & 0xFF;
			m_sels_3 = (v >> 16U) & 0xFF;
			m_sels_4 = (v >> 8U) & 0xFF;
			m_sels_5 = v & 0xFF;
		}
	};

	struct etc2_eac_rg11
	{
		etc2_eac_r11 m_c[2];
	};

	static void unpack_etc2_eac_r(const etc2_eac_r11* p, color_rgba* pPixels, uint32_t c)
	{
		const uint64_t sels = p->get_sels();

		const int base = (int)p->m_base * 8 + 4;
		const int mul = p->m_mul ? ((int)p->m_mul * 8) : 1;
		const int table = (int)p->m_table;

		for (uint32_t y = 0; y < 4; y++)
		{
			for (uint32_t x = 0; x < 4; x++)
			{
				const uint32_t shift = 45 - ((y + x * 4) * 3);
				
				const uint32_t sel = (uint32_t)((sels >> shift) & 7);
				
				int val = base + g_etc2_eac_tables[table][sel] * mul;
				val = clamp<int>(val, 0, 2047);

				// Convert to 8-bits with rounding
				pPixels[x + y * 4].m_comps[c] = static_cast<uint8_t>((val * 255 + 1024) / 2047);

			} // x
		} // y
	}

	void unpack_etc2_eac_rg(const void* p, color_rgba* pPixels)
	{
		for (uint32_t c = 0; c < 2; c++)
		{
			const etc2_eac_r11* pBlock = &static_cast<const etc2_eac_rg11*>(p)->m_c[c];

			unpack_etc2_eac_r(pBlock, pPixels, c);
		}
	}
	
	// Unpacks to RGBA, R, RG, or A
	bool unpack_block(texture_format fmt, const void* pBlock, color_rgba* pPixels)
	{
		switch (fmt)
		{
		case texture_format::cBC1:
		{
			unpack_bc1(pBlock, pPixels, true);
			break;
		}
		case texture_format::cBC3:
		{
			return unpack_bc3(pBlock, pPixels);
		}
		case texture_format::cBC4:
		{
			// Unpack to R
			unpack_bc4(pBlock, &pPixels[0].r, sizeof(color_rgba));
			break;
		}
		case texture_format::cBC5:
		{
			unpack_bc5(pBlock, pPixels);
			break;
		}
		case texture_format::cBC7:
		{
			// We only support modes 5 and 6.
			if (!unpack_bc7_mode5(pBlock, pPixels))
			{
				if (!unpack_bc7_mode6(pBlock, pPixels))
					return false;
			}

			break;
		}
		// Full ETC2 color blocks (planar/T/H modes) is currently unsupported in basisu, but we do support ETC2 with alpha (using ETC1 for color)
		case texture_format::cETC2_RGB:
		case texture_format::cETC1:
		case texture_format::cETC1S:
		{
			return unpack_etc1(*static_cast<const etc_block*>(pBlock), pPixels);
		}
		case texture_format::cETC2_RGBA:
		{
			if (!unpack_etc1(static_cast<const etc_block*>(pBlock)[1], pPixels))
				return false;
			unpack_etc2_eac(pBlock, pPixels);
			break;
		}
		case texture_format::cETC2_ALPHA:
		{
			// Unpack to A
			unpack_etc2_eac(pBlock, pPixels);
			break;
		}
		case texture_format::cASTC4x4:
		{
			const bool astc_srgb = false;
			basisu_astc::astc::decompress(reinterpret_cast<uint8_t*>(pPixels), static_cast<const uint8_t*>(pBlock), astc_srgb, 4, 4);
			break;
		}
		case texture_format::cATC_RGB:
		{
			unpack_atc(pBlock, pPixels);
			break;
		}
		case texture_format::cATC_RGBA_INTERPOLATED_ALPHA:
		{
			unpack_atc(static_cast<const uint8_t*>(pBlock) + 8, pPixels);
			unpack_bc4(pBlock, &pPixels[0].a, sizeof(color_rgba));
			break;
		}
		case texture_format::cFXT1_RGB:
		{
			unpack_fxt1(pBlock, pPixels);
			break;
		}
		case texture_format::cPVRTC2_4_RGBA:
		{
			unpack_pvrtc2(pBlock, pPixels);
			break;
		}
		case texture_format::cETC2_R11_EAC:
		{
			unpack_etc2_eac_r(static_cast<const etc2_eac_r11 *>(pBlock), pPixels, 0);
			break;
		}
		case texture_format::cETC2_RG11_EAC:
		{
			unpack_etc2_eac_rg(pBlock, pPixels);
			break;
		}
		default:
		{
			assert(0);
			// TODO
			return false;
		}
		}
		return true;
	}

	bool gpu_image::unpack(image& img) const
	{
		img.resize(get_pixel_width(), get_pixel_height());
		img.set_all(g_black_color);

		if (!img.get_width() || !img.get_height())
			return true;

		if ((m_fmt == texture_format::cPVRTC1_4_RGB) || (m_fmt == texture_format::cPVRTC1_4_RGBA))
		{
			pvrtc4_image pi(m_width, m_height);
			
			if (get_total_blocks() != pi.get_total_blocks())
				return false;
			
			memcpy(&pi.get_blocks()[0], get_ptr(), get_size_in_bytes());

			pi.deswizzle();

			pi.unpack_all_pixels(img);

			return true;
		}

		assert((m_block_width <= cMaxBlockSize) && (m_block_height <= cMaxBlockSize));
		color_rgba pixels[cMaxBlockSize * cMaxBlockSize];
		for (uint32_t i = 0; i < cMaxBlockSize * cMaxBlockSize; i++)
			pixels[i] = g_black_color;

		bool success = true;

		for (uint32_t by = 0; by < m_blocks_y; by++)
		{
			for (uint32_t bx = 0; bx < m_blocks_x; bx++)
			{
				const void* pBlock = get_block_ptr(bx, by);

				if (!unpack_block(m_fmt, pBlock, pixels))
					success = false;

				img.set_block_clipped(pixels, bx * m_block_width, by * m_block_height, m_block_width, m_block_height);
			} // bx
		} // by

		return success;
	}
		
	static const uint8_t g_ktx_file_id[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };

	// KTX/GL enums
	enum
	{
		KTX_ENDIAN = 0x04030201, 
		KTX_OPPOSITE_ENDIAN = 0x01020304,
		KTX_ETC1_RGB8_OES = 0x8D64,
		KTX_RED = 0x1903,
		KTX_RG = 0x8227,
		KTX_RGB = 0x1907,
		KTX_RGBA = 0x1908,
		KTX_COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0,
		KTX_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3,
		KTX_COMPRESSED_RED_RGTC1_EXT = 0x8DBB,
		KTX_COMPRESSED_RED_GREEN_RGTC2_EXT = 0x8DBD,
		KTX_COMPRESSED_RGB8_ETC2 = 0x9274,
		KTX_COMPRESSED_RGBA8_ETC2_EAC = 0x9278,
		KTX_COMPRESSED_RGBA_BPTC_UNORM = 0x8E8C,
		KTX_COMPRESSED_SRGB_ALPHA_BPTC_UNORM = 0x8E8D,
		KTX_COMPRESSED_RGB_PVRTC_4BPPV1_IMG = 0x8C00,
		KTX_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG = 0x8C02,
		KTX_COMPRESSED_RGBA_ASTC_4x4_KHR = 0x93B0,
		KTX_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR = 0x93D0,
		KTX_ATC_RGB_AMD = 0x8C92,
		KTX_ATC_RGBA_INTERPOLATED_ALPHA_AMD = 0x87EE,
		KTX_COMPRESSED_RGB_FXT1_3DFX = 0x86B0,
		KTX_COMPRESSED_RGBA_FXT1_3DFX = 0x86B1,
		KTX_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG = 0x9138,
		KTX_COMPRESSED_R11_EAC = 0x9270,
		KTX_COMPRESSED_RG11_EAC = 0x9272
	};
		
	struct ktx_header
	{
		uint8_t m_identifier[12];
		packed_uint<4> m_endianness;
		packed_uint<4> m_glType;
		packed_uint<4> m_glTypeSize;
		packed_uint<4> m_glFormat;
		packed_uint<4> m_glInternalFormat;
		packed_uint<4> m_glBaseInternalFormat;
		packed_uint<4> m_pixelWidth;
		packed_uint<4> m_pixelHeight;
		packed_uint<4> m_pixelDepth;
		packed_uint<4> m_numberOfArrayElements;
		packed_uint<4> m_numberOfFaces;
		packed_uint<4> m_numberOfMipmapLevels;
		packed_uint<4> m_bytesOfKeyValueData;

		void clear() { clear_obj(*this);	}
	};

	// Input is a texture array of mipmapped gpu_image's: gpu_images[array_index][level_index]
	bool create_ktx_texture_file(uint8_vec &ktx_data, const std::vector<gpu_image_vec>& gpu_images, bool cubemap_flag)
	{
		if (!gpu_images.size())
		{
			assert(0);
			return false;
		}

		uint32_t width = 0, height = 0, total_levels = 0;
		basisu::texture_format fmt = texture_format::cInvalidTextureFormat;

		if (cubemap_flag)
		{
			if ((gpu_images.size() % 6) != 0)
			{
				assert(0);
				return false;
			}
		}

		for (uint32_t array_index = 0; array_index < gpu_images.size(); array_index++)
		{
			const gpu_image_vec &levels = gpu_images[array_index];

			if (!levels.size())
			{
				// Empty mip chain
				assert(0);
				return false;
			}

			if (!array_index)
			{
				width = levels[0].get_pixel_width();
				height = levels[0].get_pixel_height();
				total_levels = (uint32_t)levels.size();
				fmt = levels[0].get_format();
			}
			else
			{
				if ((width != levels[0].get_pixel_width()) ||
				    (height != levels[0].get_pixel_height()) ||
				    (total_levels != levels.size()))
				{
					// All cubemap/texture array faces must be the same dimension
					assert(0);
					return false;
				}
			}

			for (uint32_t level_index = 0; level_index < levels.size(); level_index++)
			{
				if (level_index)
				{
					if ( (levels[level_index].get_pixel_width() != maximum<uint32_t>(1, levels[0].get_pixel_width() >> level_index)) ||
							(levels[level_index].get_pixel_height() != maximum<uint32_t>(1, levels[0].get_pixel_height() >> level_index)) )
					{
						// Malformed mipmap chain
						assert(0);
						return false;
					}
				}

				if (fmt != levels[level_index].get_format())
				{
					// All input textures must use the same GPU format
					assert(0);
					return false;
				}
			}
		}

		uint32_t internal_fmt = KTX_ETC1_RGB8_OES, base_internal_fmt = KTX_RGB;

		switch (fmt)
		{
		case texture_format::cBC1:
		{
			internal_fmt = KTX_COMPRESSED_RGB_S3TC_DXT1_EXT;
			break;
		}
		case texture_format::cBC3:
		{
			internal_fmt = KTX_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		case texture_format::cBC4:
		{
			internal_fmt = KTX_COMPRESSED_RED_RGTC1_EXT;// KTX_COMPRESSED_LUMINANCE_LATC1_EXT;
			base_internal_fmt = KTX_RED;
			break;
		}
		case texture_format::cBC5:
		{
			internal_fmt = KTX_COMPRESSED_RED_GREEN_RGTC2_EXT;
			base_internal_fmt = KTX_RG;
			break;
		}
		case texture_format::cETC1:
		case texture_format::cETC1S:
		{
			internal_fmt = KTX_ETC1_RGB8_OES;
			break;
		}
		case texture_format::cETC2_RGB:
		{
			internal_fmt = KTX_COMPRESSED_RGB8_ETC2;
			break;
		}
		case texture_format::cETC2_RGBA:
		{
			internal_fmt = KTX_COMPRESSED_RGBA8_ETC2_EAC;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		case texture_format::cBC7:
		{
			internal_fmt = KTX_COMPRESSED_RGBA_BPTC_UNORM;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		case texture_format::cPVRTC1_4_RGB:
		{
			internal_fmt = KTX_COMPRESSED_RGB_PVRTC_4BPPV1_IMG;
			break;
		}
		case texture_format::cPVRTC1_4_RGBA:
		{
			internal_fmt = KTX_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		case texture_format::cASTC4x4:
		{
			internal_fmt = KTX_COMPRESSED_RGBA_ASTC_4x4_KHR;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		case texture_format::cATC_RGB:
		{
			internal_fmt = KTX_ATC_RGB_AMD;
			break;
		}
		case texture_format::cATC_RGBA_INTERPOLATED_ALPHA:
		{
			internal_fmt = KTX_ATC_RGBA_INTERPOLATED_ALPHA_AMD;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		case texture_format::cETC2_R11_EAC:
		{
			internal_fmt = KTX_COMPRESSED_R11_EAC;
			base_internal_fmt = KTX_RED;
			break;
		}
		case texture_format::cETC2_RG11_EAC:
		{
			internal_fmt = KTX_COMPRESSED_RG11_EAC;
			base_internal_fmt = KTX_RG;
			break;
		}
		case texture_format::cFXT1_RGB:
		{
			internal_fmt = KTX_COMPRESSED_RGB_FXT1_3DFX;
			break;
		}
		case texture_format::cPVRTC2_4_RGBA:
		{
			internal_fmt = KTX_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG;
			base_internal_fmt = KTX_RGBA;
			break;
		}
		default:
		{
			// TODO
			assert(0);
			return false;
		}
		}
		
		ktx_header header;
		header.clear();
		memcpy(&header.m_identifier, g_ktx_file_id, sizeof(g_ktx_file_id));
		header.m_endianness = KTX_ENDIAN;
		
		header.m_pixelWidth = width;
		header.m_pixelHeight = height;
		
		header.m_glInternalFormat = internal_fmt;
		header.m_glBaseInternalFormat = base_internal_fmt;

		header.m_numberOfArrayElements = (uint32_t)(cubemap_flag ? (gpu_images.size() / 6) : gpu_images.size());
		if (header.m_numberOfArrayElements == 1)
			header.m_numberOfArrayElements = 0;

		header.m_numberOfMipmapLevels = total_levels;
		header.m_numberOfFaces = cubemap_flag ? 6 : 1;

		append_vector(ktx_data, (uint8_t *)&header, sizeof(header));

		for (uint32_t level_index = 0; level_index < total_levels; level_index++)
		{
			uint32_t img_size = gpu_images[0][level_index].get_size_in_bytes();
			
			if ((header.m_numberOfFaces == 1) || (header.m_numberOfArrayElements > 1))
			{
				img_size = img_size * header.m_numberOfFaces * maximum<uint32_t>(1, header.m_numberOfArrayElements);
			}

			assert(img_size && ((img_size & 3) == 0));

			packed_uint<4> packed_img_size(img_size);
			append_vector(ktx_data, (uint8_t *)&packed_img_size, sizeof(packed_img_size));

			uint32_t bytes_written = 0;

			for (uint32_t array_index = 0; array_index < maximum<uint32_t>(1, header.m_numberOfArrayElements); array_index++)
			{
				for (uint32_t face_index = 0; face_index < header.m_numberOfFaces; face_index++)
				{
					const gpu_image& img = gpu_images[cubemap_flag ? (array_index * 6 + face_index) : array_index][level_index];

					append_vector(ktx_data, (uint8_t *)img.get_ptr(), img.get_size_in_bytes());
					
					bytes_written += img.get_size_in_bytes();
				}
			
			} // array_index

		} // level_index

		return true;
	}

	bool write_compressed_texture_file(const char* pFilename, const std::vector<gpu_image_vec>& g, bool cubemap_flag)
	{
		std::string extension(string_tolower(string_get_extension(pFilename)));

		uint8_vec filedata;
		if (extension == "ktx")
		{
			if (!create_ktx_texture_file(filedata, g, cubemap_flag))
				return false;
		}
		else if (extension == "pvr")
		{
			// TODO
			return false;
		}
		else if (extension == "dds")
		{
			// TODO
			return false;
		}
		else
		{
			// unsupported texture format
			assert(0);
			return false;
		}

		return basisu::write_vec_to_file(pFilename, filedata);
	}

	bool write_compressed_texture_file(const char* pFilename, const gpu_image& g)
	{
		std::vector<gpu_image_vec> v;
		enlarge_vector(v, 1)->push_back(g);
		return write_compressed_texture_file(pFilename, v, false);
	}

	// Can't find docs. I'm assuming order in file should be "TEXC". Since
	// uint32_t's are written in little endian order we need to put T in the
	// low order byte, etc.
	const uint32_t OUT_FILE_MAGIC =
	        'T' | ('E' << 8) | ('X' << 16) | ('C' << 24);

	struct out_file_header 
	{
		packed_uint<4> m_magic;
		packed_uint<4> m_pad;
		packed_uint<4> m_width;
		packed_uint<4> m_height;
	};

	// As no modern tool supports FXT1 format .KTX files, let's write .OUT files and make sure 3DFX's original tools shipped in 1999 can decode our encoded output.
	bool write_3dfx_out_file(const char* pFilename, const gpu_image& gi)
	{
		out_file_header hdr;
		hdr.m_magic = OUT_FILE_MAGIC;
		hdr.m_pad = 0;
		hdr.m_width = gi.get_blocks_x() * 8;
		hdr.m_height = gi.get_blocks_y() * 4;

		FILE* pFile = nullptr;
#ifdef _WIN32
		fopen_s(&pFile, pFilename, "wb");
#else
		pFile = fopen(pFilename, "wb");
#endif
		if (!pFile)
			return false;

		fwrite(&hdr, sizeof(hdr), 1, pFile);
		fwrite(gi.get_ptr(), gi.get_size_in_bytes(), 1, pFile);
		
		return fclose(pFile) != EOF;
	}
} // basisu

