#pragma once

#include "game/Defines.h"

// Tech-base wall panel: top/bottom seams with vertical panel gaps
const uint8_t vectorTexture0[] PROGMEM =
{
	6,
	0, 18, 128, 18,
	0, 110, 128, 110,
	32, 18, 32, 110,
	64, 18, 64, 110,
	96, 18, 96, 110,
	48, 64, 80, 64,
};

const uint8_t vectorTexture1[] PROGMEM =
{
	6,
	0, 16, 128, 16 ,
	0, 112, 128, 112 ,
	0, 16, 0, 112,
	0, 16, 128, 112,
	0, 112, 128, 16,
	128, 16, 128, 112,

	/*	16, 16, 112, 16 ,
	16, 16, 16, 128,
	48, 16, 48, 128,
	80, 16, 80, 128,
	112, 16, 112, 128,*/
};

const uint8_t vectorTexture2[] PROGMEM =
{
	12,
	38,13,90,13,
	38,13,64,38,
	64,38,90,13,
	13,38,38,64,
	13,38,13,90,
	13,90,38,64,
	38,115,90,115,
	38,115,64,90,
	64,90,90,115,
	90,64,115,38,
	90,64,115,90,
	115,38,115,90,
};

const uint8_t* const textures[] PROGMEM =
{
	vectorTexture0,
	vectorTexture1,
	vectorTexture2,
};