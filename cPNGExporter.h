#pragma once
#include "cimageexporter.h"
#include <Windows.h>

struct pngChunkData{
	DWORD chunkDataLength;
	BYTE chunkType[4];
	//void * pChunkData;
	//DWORD CRC;
};
struct IHDR {
	DWORD Width;
	DWORD Height;
	BYTE BitDepth;
	BYTE ColorType;
	BYTE CompressionMethod;
	BYTE FilterMethod;
	BYTE InterlaceMethod;
};
class cPNGExporter :
	public cImageExporter
{
private:
	DWORD crcTable[256];
	//Utility
	DWORD update_crc(DWORD crc, BYTE *buf, int len);
	DWORD crc(BYTE *buf, int len);
public:
	bool ExportImageData(void * pImageData, int Width, int Height, const char * FileName);
	cPNGExporter(void);
	~cPNGExporter(void);
};

