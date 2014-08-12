#pragma once
//ABTRACT
class cImageExporter
{
protected:

public:
	virtual bool ExportImageData(void * pImageData, int Width, int Height, const char * FileName)=0;
};

