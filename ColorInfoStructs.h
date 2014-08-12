#pragma once
#include <GdiPlus.h>

struct TextColorInfo{
	const static unsigned long FILL_GRADIENT = 0x00000001;
	const static unsigned long HAS_OUTLINE   = 0x00000002;
	const static unsigned long HAS_SHADOW    = 0x00000004;
	const static unsigned long ANTI_ALIASED  = 0x00000008;
	const static unsigned char OUTLINE_HARD  = 0xF0;
	const static unsigned char OUTLINE_GLOW  = 0x0F;

	const static int DefaultSize = 120;
	int Size;
	int OutlineWidth;
	unsigned char OutlineType;
	unsigned long Attributes;
	unsigned long dwStyle;  //Bold, Italics, Underline...
	Gdiplus::Color BaseColor;
	Gdiplus::Color GradientEndColor;
	float GradientAngle;
	Gdiplus::Color OutlineColor;
	Gdiplus::Color ShadowColor;
	int SSAAMultiplier;
};