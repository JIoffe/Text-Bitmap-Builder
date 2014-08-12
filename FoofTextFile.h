#pragma once
struct FoofText{
	static const unsigned char ASCII_256 = 0x01;
};
struct FoofTextFileHeader{
	unsigned short ImageWidth;
	unsigned char FontType;  // determines how many letters are in here

};
struct FoofTextCharacter{

};