#include <Windows.h>
#include <cstdio>
#include <Commdlg.h>
#include <commctrl.h>
#include <GdiPlus.h>  // once again, use the powers of GDI+ to do most of the work
#include <Commdlg.h>
#include <commctrl.h>
#include <math.h>
#include "resource.h"

#include "cImageExporter.h"
#include "cPNGExporter.h"
#include "ColorInfoStructs.h"

using namespace Gdiplus;
using namespace std;

#pragma comment (lib,"Gdiplus.lib")
#pragma comment (lib,"Comctl32.lib ")
#pragma comment (lib,"Comdlg32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

const float DEGTORAD = 0.0174532925f;
OPENFILENAME sfn;
char sfPath[MAX_PATH];
TextColorInfo TextInfo;
bool bTabFillSolid;
Color bgColor(0,0,0);
HINSTANCE AppInstance = NULL;
HWND hPreview = NULL;
HWND hControls = NULL;
HWND hCurrentFillPanel = NULL;
HWND hOutlinePanel = NULL;
Bitmap * pPreviewFrame = NULL;
FontFamily*  pFontFamily = NULL;
FontFamily *pCurrentFontFamily = NULL;
CHOOSECOLOR cc;
DWORD rgbCurrent; 
COLORREF UserCustomColors[16];

Bitmap * pTextBitmap = NULL;
const int TextBitmapWidth = 1024;
Bitmap * pTextBitmapSSAA = NULL; //We want to actually render to this.
Bitmap * pGradientBrushImage = NULL; // This will be used to paint a gradient over each letter
UINT16 PreviewScrollX = 0, PreviewScrollY = 0;
int PreviewWindowWidth = 0, PreviewWindowHeight = 0;
int MaxScrollY = 0, MaxScrollX = 0;
float fZoom = 1.0f;
void UpdateScrollBars(){
	HWND hVScroll = GetDlgItem(hControls, IDC_PREVIEWVSCROLL);
	HWND hHScroll = GetDlgItem(hControls, IDC_PREVIEWHSCROLL);
	MaxScrollY = max(1, (float)TextBitmapWidth*fZoom - PreviewWindowHeight);
	MaxScrollX = max(1, (float)TextBitmapWidth*fZoom - PreviewWindowWidth);
	PreviewScrollY = min(MaxScrollY, PreviewScrollY);
	PreviewScrollX = min(MaxScrollX, PreviewScrollX);
	SCROLLINFO si;
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_RANGE | SIF_POS | SIF_PAGE;
	si.nMax = MaxScrollY;
	si.nMin = 0;
	si.nPos = PreviewScrollY;
	si.nPage = PreviewWindowHeight/MaxScrollY;

	SetScrollInfo(hVScroll, SB_CTL, &si, TRUE);
	si.nPos = PreviewScrollX;
	si.nMax = MaxScrollX;
	si.nPage = PreviewWindowWidth/MaxScrollX;
	SetScrollInfo(hHScroll, SB_CTL, &si, TRUE);
	return;
}
void FillClientWithColor(HWND hwnd, Color FillColor){
	HDC hDC = GetDC(hwnd);
	Graphics g(hDC);
	RECT ColorBlockRect;
	GetClientRect(hwnd, &ColorBlockRect);
	SolidBrush solidcolorBrush(FillColor);
	g.FillRectangle(&solidcolorBrush, 0, 0, ColorBlockRect.right, ColorBlockRect.bottom);
	return;
}
void FillClientWithImage(HWND hwnd, Image * pImage){
	RECT rPreviewRect;
	GetClientRect(hwnd, &rPreviewRect);
	Rect Destination;
	Destination.X = 0;
	Destination.Y = 0;
	Destination.Width = rPreviewRect.right;
	Destination.Height = rPreviewRect.bottom;
	HDC hDC = GetDC(hwnd);
	Graphics g(hDC);
	g.DrawImage(pImage, Destination);
	return;
}
void UpdateGradientImage(){
	if(pGradientBrushImage){
		int BrushWidth = (TextInfo.SSAAMultiplier * TextBitmapWidth) / 16;
		float AngleAsRad = TextInfo.GradientAngle*DEGTORAD;
		LinearGradientBrush gradientBrush(
				   Point(0, 0),
				   Point(BrushWidth * cos((double)AngleAsRad), BrushWidth * sin((double)AngleAsRad)),
				   TextInfo.BaseColor,
				   TextInfo.GradientEndColor);
		gradientBrush.SetWrapMode(WrapModeTileFlipXY);
		Graphics g(pGradientBrushImage);
		g.FillRectangle(&gradientBrush, 0,0, BrushWidth, BrushWidth);
	}
	return;
}
void UpdateSwatchPreviews(){
	FillClientWithColor(GetDlgItem(hCurrentFillPanel, IDC_TXTFILLPREVIEW ), TextInfo.BaseColor);
	FillClientWithColor(GetDlgItem(hOutlinePanel, IDC_TXTOUTLINEPREVIEW ), TextInfo.OutlineColor);
	FillClientWithColor(GetDlgItem(hControls, IDC_BGPREVIEW), bgColor);
	if(!bTabFillSolid){
		//We need to update the gradients as well
		FillClientWithColor(GetDlgItem(hCurrentFillPanel, IDC_TXTGRADENDPREVIEW ), TextInfo.GradientEndColor);
		FillClientWithImage(GetDlgItem(hCurrentFillPanel, IDC_GRADPREVIEW), pGradientBrushImage);
	}
}
void UpdatePreview(){
	if(hPreview){
		Graphics g(GetDC(hPreview));
		//DrawBG
		SolidBrush bgBrush(bgColor);
		g.FillRectangle(&bgBrush, 0,0,PreviewWindowWidth, PreviewWindowHeight);
		if(pTextBitmap){
			Rect Destination;
			Destination.X = -PreviewScrollX;
			Destination.Y = -PreviewScrollY;
			Destination.Width = TextBitmapWidth * fZoom;
			Destination.Height = TextBitmapWidth * fZoom;
			g.DrawImage(pTextBitmap, Destination);
			//g.DrawImage(pTextBitmap,0,0);
		}

	}
}
void PopulateFontList(HWND hwnd){
	InstalledFontCollection installedFontCollection;
	INT nFonts = installedFontCollection.GetFamilyCount();
	int Found = 0;
	pFontFamily = new FontFamily[nFonts];
	installedFontCollection.GetFamilies(nFonts, pFontFamily, &Found);
	WCHAR wcstrName[LF_FACESIZE];
	for(int i = 0; i < nFonts; i++){
		pFontFamily[i].GetFamilyName(wcstrName);
		char cstrName[LF_FACESIZE];
		WideCharToMultiByte(CP_UTF8, 0, wcstrName, -1, cstrName, LF_FACESIZE, NULL, NULL);
		SendMessage(hwnd, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)cstrName);
		SendMessage(hwnd, (UINT)CB_SETITEMDATA, (WPARAM)i, (LPARAM)&pFontFamily[i]);
	}
	//Start with the first font, also helps the user understand what this is for
	SendMessage(hwnd, (UINT) CB_SETCURSEL, (WPARAM)0, NULL);

	return;
}
void RedrawFont(){
		if(pCurrentFontFamily){
			if(pTextBitmapSSAA){
				StringFormat Format;
				Format.SetAlignment(StringAlignmentCenter);
				Format.SetLineAlignment(StringAlignmentCenter);

				Graphics g(pTextBitmapSSAA);
				//Clrscn
				g.Clear(Color(0,0,0,0));
				int SSAATargetWidth = TextBitmapWidth * TextInfo.SSAAMultiplier;
				int RectWidth = (SSAATargetWidth / 16);

				WCHAR i = 0;
				Pen outlinePen(TextInfo.OutlineColor, TextInfo.OutlineWidth * TextInfo.SSAAMultiplier);
				outlinePen.SetLineJoin(LineJoinRound);
				Pen borderPen(Color(255,255,0),1);

				//Determine largest text size that can fit in each square.
				//  - user defined padding
				
				//Get CellHeight in design units
				UINT16 CellHeight = pCurrentFontFamily->GetCellAscent(FontStyleRegular) + pCurrentFontFamily->GetCellDescent(FontStyleRegular)*2;
				//UINT16 CellHeight = pCurrentFontFamily->GetLineSpacing(FontStyleRegular);
				UINT16 EmHeight = pCurrentFontFamily->GetEmHeight(FontStyleRegular);

				//Solve for what x font size gets us a cell height of RectWidth;
				float TextSize = (float)RectWidth / ((float)CellHeight / EmHeight);
				GraphicsPath TextPath;

				for(int x = 0; x < 16; x++){
					int xRW = x * RectWidth;
					if(IsDlgButtonChecked(hControls, IDC_CKDRAWBORDERS) == BST_CHECKED){
						g.DrawLine(&borderPen, Point(xRW, 0), Point(xRW, SSAATargetWidth));
						g.DrawLine(&borderPen, Point(0, xRW), Point(SSAATargetWidth, xRW));
					}
					for(int y = 0; y < 16; y++){
						int yRW = y * RectWidth;
						RectF LetterRect(xRW, yRW, RectWidth, RectWidth); 
						TextPath.AddString(&i,1, pCurrentFontFamily, 0, TextSize, LetterRect , &Format);
						i++;
					}
				}
				if(TextInfo.OutlineType == TextColorInfo::OUTLINE_HARD){
					g.DrawPath(&outlinePen, &TextPath);
				}else if(TextInfo.OutlineType == TextColorInfo::OUTLINE_GLOW){
					//Redraw outline multiple times
					int OutlineSteps = 8;
					int StepWidth = TextInfo.OutlineWidth * TextInfo.SSAAMultiplier / OutlineSteps;
					int AlphaStep = 255 / OutlineSteps;
					for(int i = OutlineSteps; i > 0; i--){
						if(i == 1)
							AlphaStep = 255;
							Color stepColor( AlphaStep, TextInfo.OutlineColor.GetRed(), TextInfo.OutlineColor.GetGreen(), TextInfo.OutlineColor.GetBlue());
							int penWidth = i * StepWidth;
							Pen glowPen(stepColor, penWidth);
							glowPen.SetLineJoin(LineJoinRound);
							g.DrawPath(&glowPen, &TextPath);
						}
				}
				Brush * pBrush = NULL;
				if(bTabFillSolid){
					pBrush = new SolidBrush(TextInfo.BaseColor);
				}else{
					//Bit gradient or texture
					pBrush = new TextureBrush(pGradientBrushImage);
				}
				g.FillPath(pBrush, &TextPath);
				delete pBrush;
				if(pTextBitmap){
					Graphics finalG(pTextBitmap);
					finalG.Clear(Color(0,0,0,0));
					Rect BitmapRect;
					BitmapRect.X = 0;
					BitmapRect.Y = 0;
					BitmapRect.Height = TextBitmapWidth;
					BitmapRect.Width = TextBitmapWidth;
					finalG.DrawImage(pTextBitmapSSAA, BitmapRect);
				}
			}
		}
	return;
}


void CreateChildDialog(HWND hChild, HWND hOwner, int AnchorID){
	RECT AnchorRect;
	GetWindowRect(GetDlgItem(hOwner, AnchorID), &AnchorRect);
	POINT AnchorPoint;
	AnchorPoint.x = AnchorRect.left;
	AnchorPoint.y = AnchorRect.top;
	ScreenToClient(hOwner, &AnchorPoint);
	SetWindowPos(hChild, NULL, AnchorPoint.x, AnchorPoint.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	UpdateWindow(hChild);
	ShowWindow(hChild, SW_SHOWDEFAULT);
	return;
}
BOOL CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
	switch(msg){
		case WM_VSCROLL:{
            int yNewPos;    
			switch (LOWORD(wParam)){ 
                case SB_PAGEUP: 
                    yNewPos = PreviewScrollY - 50; 
                    break; 
                case SB_PAGEDOWN: 
                    yNewPos = PreviewScrollY + 50; 
                    break; 
                case SB_LINEUP: 
                    yNewPos = PreviewScrollY - 5; 
                    break; 
                case SB_LINEDOWN: 
                    yNewPos = PreviewScrollY + 5; 
                    break; 
                case SB_THUMBPOSITION: 
                    yNewPos = HIWORD(wParam); 
                    break; 
				case SB_THUMBTRACK:   //Constantly update as we drag
					yNewPos = HIWORD(wParam);
					break;
                default: 
                    yNewPos = PreviewScrollY; 
					break;
            } 
			yNewPos = max(0, yNewPos);
			yNewPos = min(MaxScrollY, yNewPos);

			if(yNewPos == PreviewScrollY){
				break;
			}
			PreviewScrollY = yNewPos;
			SCROLLINFO si;
			si.cbSize = sizeof(si); 
            si.fMask  = SIF_POS; 
            si.nPos   = yNewPos; 
            SetScrollInfo((HWND)lParam, SB_CTL, &si, TRUE); 
			UpdatePreview();
			break;
		}
		case WM_HSCROLL:{
            int xNewPos;    
			switch (LOWORD(wParam)){ 
                case SB_PAGEUP: 
                    xNewPos = PreviewScrollX - 50; 
                    break; 
                case SB_PAGEDOWN: 
					xNewPos = PreviewScrollX + 50; 
                    break; 
                case SB_LINEUP: 
                    xNewPos = PreviewScrollX - 5; 
                    break; 
                case SB_LINEDOWN: 
                    xNewPos = PreviewScrollX + 5; 
                    break; 
                case SB_THUMBPOSITION: 
                    xNewPos = HIWORD(wParam); 
                    break; 
				case SB_THUMBTRACK:   //Constantly update as we drag
					xNewPos = HIWORD(wParam);
					break;
                default: 
                    xNewPos = PreviewScrollX; 
					break;
            } 
			xNewPos = max(0, xNewPos);
			xNewPos = min(MaxScrollX, xNewPos);

			if(xNewPos == PreviewScrollX){
				break;
			}
			PreviewScrollX = xNewPos;
			SCROLLINFO si;
			si.cbSize = sizeof(si); 
            si.fMask  = SIF_POS; 
            si.nPos   = xNewPos; 
            SetScrollInfo((HWND)lParam, SB_CTL, &si, TRUE); 
			UpdatePreview();
			break;
		}
		case WM_CLOSE:{
			EndDialog(hwnd, 0);
			break;
		}
		case WM_COMMAND:{
			switch(LOWORD(wParam)){
				case IDABOUTOK:{
					EndDialog(hwnd, 0);
					break;
				}
				case IDC_BGCOLOR:{
					if (ChooseColor(&cc)==TRUE){ 
						BYTE R = GetRValue(cc.rgbResult);
						BYTE G = GetGValue(cc.rgbResult);
						BYTE B = GetBValue(cc.rgbResult);
						bgColor = Color(R,G,B);
						HWND PreviewBlock = GetDlgItem(hwnd, IDC_BGPREVIEW);

						FillClientWithColor(PreviewBlock, bgColor);
						UpdatePreview();
					}
					break;
				}
				case IDC_TEXTFILL:{
					if (ChooseColor(&cc)==TRUE){ 
						BYTE R = GetRValue(cc.rgbResult);
						BYTE G = GetGValue(cc.rgbResult);
						BYTE B = GetBValue(cc.rgbResult);
						TextInfo.BaseColor = Color(R,G,B);
						HWND PreviewBlock = GetDlgItem(hwnd, IDC_TXTFILLPREVIEW);

						FillClientWithColor(PreviewBlock, TextInfo.BaseColor);
						if(!bTabFillSolid){
							//Update our gradient preview too.
							UpdateGradientImage();
							FillClientWithImage(GetDlgItem(hCurrentFillPanel, IDC_GRADPREVIEW), pGradientBrushImage);
						}
						RedrawFont();
						UpdatePreview();
					}
					break;
				}
				case IDC_GRADCOLOR:{
					if (ChooseColor(&cc)==TRUE){ 
						BYTE R = GetRValue(cc.rgbResult);
						BYTE G = GetGValue(cc.rgbResult);
						BYTE B = GetBValue(cc.rgbResult);
						TextInfo.GradientEndColor = Color(R,G,B);
						HWND PreviewBlock = GetDlgItem(hwnd, IDC_TXTGRADENDPREVIEW);

						FillClientWithColor(PreviewBlock, TextInfo.GradientEndColor);
						//Update our gradient preview too.
						UpdateGradientImage();
						FillClientWithImage(GetDlgItem(hCurrentFillPanel, IDC_GRADPREVIEW), pGradientBrushImage);
						RedrawFont();
						UpdatePreview();
					}
					break;
				}

				case IDC_OUTLINECOLOR:{
					if (ChooseColor(&cc)==TRUE){ 
						BYTE R = GetRValue(cc.rgbResult);
						BYTE G = GetGValue(cc.rgbResult);
						BYTE B = GetBValue(cc.rgbResult);
						TextInfo.OutlineColor = Color(R,G,B);

						HWND PreviewBlock = GetDlgItem(hwnd, IDC_TXTOUTLINEPREVIEW);
						FillClientWithColor(PreviewBlock, TextInfo.OutlineColor);
						RedrawFont();
						UpdatePreview();
					}

					break;
				}
				case IDC_CKDRAWBORDERS:{
					RedrawFont();
					UpdatePreview();
					break;
				}
				case CM_ZOOMIN:{
					fZoom += 0.15f;
					UpdatePreview();
					UpdateScrollBars();
					break;
				}
				case CM_ZOOMOUT:{
					fZoom -= 0.15f;
					UpdatePreview();
					UpdateScrollBars();
					break;
				}
				case CM_ZOOMFULL:{
					fZoom = 1.0f;
					UpdatePreview();
					UpdateScrollBars();
					break;
				}
				case CM_FITWINDOW:{
					fZoom = (float)PreviewWindowWidth / TextBitmapWidth;
					UpdateScrollBars();
					PreviewScrollX = 0;
					PreviewScrollY = 0;
					UpdatePreview();
					break;
				}
				default:
					break;
			}
			switch(HIWORD(wParam)){
				case EN_CHANGE:{
					DWORD ID = LOWORD(wParam);
					switch(ID){
						case IDC_EDITTEXTSIZE:{
							TextInfo.Size = GetDlgItemInt(hwnd, ID, NULL, NULL);
							RedrawFont();
							UpdatePreview();
							break;
						}
						case IDC_EDITOUTLINEWIDTH:{
							TextInfo.OutlineWidth = GetDlgItemInt(hwnd, ID, NULL, NULL);
							RedrawFont();
							UpdatePreview();
							break;
						}
						case IDC_GRADANGLE:{
							TextInfo.GradientAngle = GetDlgItemInt(hwnd, ID, NULL, NULL);
							UpdateGradientImage();
							FillClientWithImage(GetDlgItem(hCurrentFillPanel, IDC_GRADPREVIEW), pGradientBrushImage);
							RedrawFont();
							UpdatePreview();
							break;
						}
						default: 
							break;
					}
					break;
				}
				case CBN_SELCHANGE:{
					DWORD ID = LOWORD(wParam);
					HWND hCombo = GetDlgItem(hwnd, ID);
					switch(ID){
						case IDC_FONTCOMBO:{		
							int Selection = (int)SendMessage(hCombo, (UINT) CB_GETCURSEL, NULL, NULL);
							pCurrentFontFamily = (FontFamily*)SendMessage(hCombo, (UINT) CB_GETITEMDATA, (WPARAM)Selection, NULL);
							RedrawFont();
							UpdatePreview();
							break;
						}
						case IDC_COMBOAA:{
							int Selection = (int)SendMessage(hCombo, (UINT) CB_GETCURSEL, NULL, NULL);
							TextInfo.SSAAMultiplier = (int)SendMessage(hCombo, (UINT) CB_GETITEMDATA, (WPARAM)Selection, NULL);
							int SSAATargetWidth = TextBitmapWidth * TextInfo.SSAAMultiplier;
							if(pTextBitmapSSAA){
								delete pTextBitmapSSAA;
							}
								pTextBitmapSSAA = new Bitmap(SSAATargetWidth, SSAATargetWidth);

							if(pGradientBrushImage){
								delete pGradientBrushImage;
							}
							pGradientBrushImage = new Bitmap(SSAATargetWidth/16, SSAATargetWidth/16);
							UpdateGradientImage();
							if(!bTabFillSolid){
								FillClientWithImage(GetDlgItem(hCurrentFillPanel, IDC_GRADPREVIEW), pGradientBrushImage);
							}
							RedrawFont();
							UpdatePreview();
							break;
						}
						default:
							break;
					}
					break;
				}
				case BN_CLICKED:{
					DWORD ID = LOWORD(wParam);
					if(ID == IDC_RADSOLID || ID == IDC_RADGRAD){
						bool bChanged = false;
						if(ID == IDC_RADSOLID){
							if(!bTabFillSolid){
								if(IsDlgButtonChecked(hControls, IDC_RADSOLID) == BST_CHECKED){
									bTabFillSolid = true;
									bChanged = true;
									if(hCurrentFillPanel){
										EndDialog(hCurrentFillPanel, 0);
									}
									hCurrentFillPanel = CreateDialog(AppInstance, MAKEINTRESOURCE(IDD_SOLIDFILL), hControls, DlgProc);
								}
							}
						}
						else{
							if(bTabFillSolid){
								if(IsDlgButtonChecked(hControls, IDC_RADGRAD) == BST_CHECKED){
									bTabFillSolid = false;
									bChanged = true;
									if(hCurrentFillPanel){
										EndDialog(hCurrentFillPanel, 0);
									}
									hCurrentFillPanel = CreateDialog(AppInstance, MAKEINTRESOURCE(IDD_GRADIENTFILL), hControls, DlgProc);
									SendMessage(GetDlgItem(hCurrentFillPanel, IDC_SPIN1), (UINT)UDM_SETRANGE, NULL, MAKELPARAM(90,0));
								}
							}
						}
						if(bChanged){
							CreateChildDialog(hCurrentFillPanel, hControls, IDC_FILLHOOK);
							RedrawFont();
							UpdateSwatchPreviews();
							UpdatePreview();
						}
					}else{
						if(ID == IDC_RADOUTLINE){
							if((IsDlgButtonChecked(hwnd, ID) == BST_CHECKED)){
								TextInfo.OutlineType = TextColorInfo::OUTLINE_HARD;
								RedrawFont();
								UpdateSwatchPreviews();
								UpdatePreview();
							}
						}else if(ID == IDC_RADGLOW){
							if((IsDlgButtonChecked(hwnd, ID) == BST_CHECKED)){
								TextInfo.OutlineType = TextColorInfo::OUTLINE_GLOW;
								RedrawFont();
								UpdateSwatchPreviews();
								UpdatePreview();
							}
						}else if(ID == IDC_RADNOOUTLINE){
							if((IsDlgButtonChecked(hwnd, ID) == BST_CHECKED)){
								TextInfo.OutlineType = 0;
								RedrawFont();
								UpdateSwatchPreviews();
								UpdatePreview();
							}
						}
					}
					break;
				}
				default:
					break;
			}

			break;
		}
		default:
			return FALSE;
	}
	return TRUE;
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg){
		case WM_CLOSE:{
			DestroyWindow(hwnd);
			break;
		}
		case WM_COMMAND:{
			switch(LOWORD(wParam)){
				case IDM_SAVE1:{
					if(pTextBitmap){
						if(GetSaveFileName(&sfn) != 0){
							
							//BitmapData bmpData;
							//Rect LockRect(0,0, TextBitmapWidth, TextBitmapWidth);
							//cImageExporter * pExporter = NULL;
							//pExporter = new cPNGExporter();
							//pTextBitmap->LockBits(&LockRect, ImageLockModeRead, PixelFormat32bppARGB, &bmpData);
							//pExporter->ExportImageData(bmpData.Scan0, TextBitmapWidth, TextBitmapWidth, sfn.lpstrFile);
							//pTextBitmap->UnlockBits(&bmpData);
							

						}
					}
					break;
				}
				case IDM_EXIT1:{
					DestroyWindow(hwnd);
					break;
				}
				case IDM_ABOUT1:{
					DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ABOUT), hwnd, DlgProc);
					break;
				}
				default:
					break;
			}
			break;
		}
		case WM_MOVE:{
			UpdatePreview();
			UpdateSwatchPreviews();
			//Update everything in case we got clipped
			break;
		}
		case WM_DESTROY:{
			PostQuitMessage(0);
			break;
		}
		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}
INT APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow){
	WNDCLASSEX wc;
	MSG msg;
	HWND hAppWnd;

	GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR           gdiplusToken;
  
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	InitCommonControls();
	AppInstance = hInstance;

	ZeroMemory( &sfn , sizeof(OPENFILENAME));
	sfn.lStructSize = sizeof (OPENFILENAME);
	sfn.lpstrFile = sfPath ;
	sfn.nMaxFile = MAX_PATH;
	sfn.lpstrFilter = "PNG Image\0*.png\0\0";
	sfn.nFilterIndex = 1;
	sfn.hInstance = hInstance;
	sfn.Flags= /*OFN_SHOWHELP | */OFN_OVERWRITEPROMPT;

	//Set Some default values
	TextInfo.BaseColor.SetValue(0xFFFFFFFF);
	TextInfo.GradientAngle = 90.0f;
	TextInfo.GradientEndColor.SetValue(0xFF000000);
	TextInfo.Attributes = 0;
	TextInfo.OutlineColor.SetValue(0xFF000000);
	TextInfo.OutlineWidth = 5;
	TextInfo.SSAAMultiplier = 1; // By default we won't use AA...

	pTextBitmap = new Bitmap(TextBitmapWidth, TextBitmapWidth);
	int SSAATargetWidth = TextBitmapWidth * TextInfo.SSAAMultiplier;
	pTextBitmapSSAA = new Bitmap(SSAATargetWidth, SSAATargetWidth);
	pGradientBrushImage = new Bitmap(SSAATargetWidth/16, SSAATargetWidth/16);



	ZeroMemory(&wc, sizeof(WNDCLASSEX));
	wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.lpszMenuName  = MAKEINTRESOURCE(IDR_MENU1);
    wc.lpszClassName = "FoFoBu";
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

	if(!RegisterClassEx(&wc))
    {
        return 0;
    }

    hAppWnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        "FoFoBu",
        "Foofles Font Bitmap Builder",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 240, 120,
        NULL, NULL, hInstance, NULL);

	if(hAppWnd == NULL){
		return 0;
	}

	hControls = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_FONTMAKER1), hAppWnd, DlgProc);

	ZeroMemory(&cc, sizeof(cc));
	cc.lStructSize = sizeof(cc);
	cc.hwndOwner = hControls;
	cc.lpCustColors = (LPDWORD) UserCustomColors;
	cc.rgbResult = rgbCurrent;
	cc.Flags = CC_FULLOPEN | CC_RGBINIT;

	RECT PanelRect;
	GetWindowRect(hControls, &PanelRect);
	AdjustWindowRectEx(&PanelRect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, TRUE, WS_EX_CLIENTEDGE);
	//Resize the window to accept the dialog box
	SetWindowPos(hAppWnd, NULL, 0, 0, PanelRect.right - PanelRect.left, PanelRect.bottom - PanelRect.top, SWP_NOMOVE | SWP_NOZORDER);
	//And place the dialog at 0,0 in the window
	SetWindowPos(hControls, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER); 

	//Populate constant panels 
	hOutlinePanel = CreateDialog(AppInstance, MAKEINTRESOURCE(IDD_OUTLINEPANEL), hControls, DlgProc);
	CreateChildDialog(hOutlinePanel, hControls, IDC_OUTLINEPANELANCHOR);
	//Default to NO OUTLINE
	SendMessage(GetDlgItem(hOutlinePanel, IDC_RADNOOUTLINE), (UINT)BM_SETCHECK, (WPARAM)BST_CHECKED, NULL);

	ShowWindow(hAppWnd, SW_SHOWDEFAULT);
	UpdateWindow(hAppWnd);
	UpdateWindow(hControls);
	ShowWindow(hControls, SW_SHOWDEFAULT);

	hPreview = GetDlgItem(hControls, IDC_BITMAPPANEL);
	{
		RECT PrevRect;
		GetClientRect(hPreview, &PrevRect);
		PreviewWindowWidth = PrevRect.right;
		PreviewWindowHeight = PrevRect.bottom;
	}
	UpdatePreview();
	//Set default to SOLID COLOR fill
	SendMessage(GetDlgItem(hControls, IDC_RADSOLID), (UINT)BM_SETCHECK, (WPARAM)BST_CHECKED, NULL);
	bTabFillSolid = false;
	SendMessage(hControls, (UINT)WM_COMMAND, MAKEWPARAM(IDC_RADSOLID, BN_CLICKED), (LPARAM)GetDlgItem(hControls, IDC_RADSOLID));

	//Set defaults
	{
		char DefaultSize[MAX_PATH];
		sprintf(DefaultSize, "%i", TextColorInfo::DefaultSize);
		SetWindowText(GetDlgItem(hControls, IDC_EDITTEXTSIZE), DefaultSize);

		SetDlgItemInt(hOutlinePanel, IDC_EDITOUTLINEWIDTH, TextInfo.OutlineWidth ,0);

		//Set Spinnaz
		SendMessage(GetDlgItem(hControls, IDC_SPIN1), (UINT)UDM_SETRANGE, NULL, MAKELPARAM(1000,0));
		SendMessage(GetDlgItem(hOutlinePanel, IDC_SPIN2), (UINT)UDM_SETRANGE, NULL, MAKELPARAM(1000,0));

		FillClientWithColor(GetDlgItem(hControls, IDC_BGPREVIEW), bgColor);
		//Set Combos for SSAA and resolution
		HWND AACombo = GetDlgItem(hControls, IDC_COMBOAA);
		SendMessage(AACombo, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)"No AA");
		SendMessage(AACombo, (UINT)CB_SETITEMDATA, (WPARAM)0, (LPARAM)1);
		SendMessage(AACombo, (UINT)CB_ADDSTRING, (WPARAM)1, (LPARAM)"2xSSAA");
		SendMessage(AACombo, (UINT)CB_SETITEMDATA, (WPARAM)1, (LPARAM)2);
		SendMessage(AACombo, (UINT)CB_ADDSTRING, (WPARAM)2, (LPARAM)"4xSSAA");
		SendMessage(AACombo, (UINT)CB_SETITEMDATA, (WPARAM)2, (LPARAM)4);
		SendMessage(AACombo, (UINT)CB_ADDSTRING, (WPARAM)3, (LPARAM)"8xSSAA");
		SendMessage(AACombo, (UINT)CB_SETITEMDATA, (WPARAM)3, (LPARAM)8);
		SendMessage(AACombo, (UINT) CB_SETCURSEL, (WPARAM)0, NULL);

		//Init ScrollBs
		UpdateScrollBars();
	}
	//Setup the combo box with the current fonts
	PopulateFontList(GetDlgItem(hControls, IDC_FONTCOMBO));
	UpdateSwatchPreviews();
	while(GetMessage(&msg, NULL, 0, 0) > 0){
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}