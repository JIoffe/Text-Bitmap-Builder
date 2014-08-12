#include <stdio.h>
#include "cPNGExporter.h"


cPNGExporter::cPNGExporter(void)
{
	//construct the CRC table
	// (taken from pngsite)
	 DWORD c;
   
	for(int n = 0; n < 256; n++) {
		c = (DWORD)n;
		for (int k = 0; k < 8; k++) {
			if (c & 1){
				c = 0xedb88320L ^ (c >> 1);
			}
			else{
				c = c >> 1;
			}
		}
		crcTable[n] = c;
	}
}


cPNGExporter::~cPNGExporter(void)
{
}

DWORD cPNGExporter::update_crc(DWORD crc, BYTE *buf, int len){
     unsigned long c = crc;
     int n;
  
     for (n = 0; n < len; n++) {
       c = crcTable[(c ^ buf[n]) & 0xff] ^ (c >> 8);
     }
     return c;
}
/* Return the CRC of the bytes buf[0..len-1]. */
DWORD cPNGExporter::crc(unsigned char *buf, int len){
     return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}
bool cPNGExporter::ExportImageData(void * pImageData, int Width, int Height, const char * FileName){
	UINT * pPixelData = (UINT*)pImageData;
	FILE * pFileOut = fopen(FileName, "w");
	if(pFileOut){
		//First 8 BYTES = 137 80 78 71 13 10 26 10
		BYTE pngSignature[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
		fwrite(pngSignature, sizeof(BYTE), 8, pFileOut);
	
		pngChunkData ChunkData;
		DWORD crc = 0xFFFFFFFF;
		IHDR Header;

		ChunkData.chunkDataLength = sizeof(IHDR);
		ChunkData.chunkType[0] = 'I';
		ChunkData.chunkType[1] = 'H';
		ChunkData.chunkType[2] = 'D';
		ChunkData.chunkType[3] = 'R';

		Header.Width = Width;
		Header.Height = Height;
		Header.BitDepth = 8;  //R8G8B8A8
		Header.ColorType = 6;  //Color and Alpha Used.
		Header.CompressionMethod = 0;   
		Header.FilterMethod = 0;
		Header.InterlaceMethod = 0;

		fwrite((void*)&ChunkData, sizeof(pngChunkData), 1, pFileOut);
		fwrite((void*)&Header,sizeof(IHDR), 1, pFileOut);
		crc = this->update_crc(crc, (BYTE*)&Header, sizeof(IHDR));
		fwrite((void*)&crc, sizeof(DWORD), 1, pFileOut);

		//Now we tackle the image data... oh lawd.
		//First we need to get the color + filter data
		BYTE * FilteredImageData = new BYTE[(Width*Height*4) + Height]; //4 BYTES PER PIXEL + 1 byte per scanline
		int i = 0;
		for(int y = 0; y < Height; y++){
			FilteredImageData[i++] = 0; // no filtering...
			for(int x = 0; x < Width; x++){
				//PNG wants RGBA
				int byteindex = (x + (y*Width)) * 4;
				FilteredImageData[i++] = pPixelData[byteindex+1];
				FilteredImageData[i++] = pPixelData[byteindex+2];
				FilteredImageData[i++] = pPixelData[byteindex+3];
				FilteredImageData[i++] = pPixelData[byteindex];
			}
		}
		//COMPRESSOR

		//END //
		ChunkData.chunkType[0] = 'I';
		ChunkData.chunkType[1] = 'E';
		ChunkData.chunkType[2] = 'N';
		ChunkData.chunkType[3] = 'D';
		ChunkData.chunkDataLength = 0;
		fwrite((void*)&ChunkData, sizeof(pngChunkData), 1, pFileOut);
		crc = this->update_crc(crc, (BYTE*)&Header, sizeof(IHDR));
		fwrite((void*)&crc, sizeof(DWORD), 1, pFileOut);
		fclose(pFileOut);
	}
	else{
		//Problemo
		return false;
	}
	return true;
}