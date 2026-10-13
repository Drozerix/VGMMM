// Header file for VGM file handling

typedef struct _vgm_file_header
{
	unsigned int fccVGM;
	unsigned int lngEOFOffset;
	unsigned int lngVersion;
	unsigned int lngHzPSG;
	unsigned int lngHzYM2413;
	unsigned int lngGD3Offset;
	unsigned int lngTotalSamples;
	unsigned int lngLoopOffset;
	unsigned int lngLoopSamples;
	unsigned int lngRate;
	unsigned short int shtPSG_Feedback;
	unsigned char bytPSG_SRWidth;
	unsigned char bytPSG_Flags;
	unsigned int lngHzYM2612;
	unsigned int lngHzYM2151;
	unsigned int lngDataOffset;
	unsigned int lngHzSPCM;
	unsigned int lngSPCMIntf;
	unsigned int lngHzRF5C68;
	unsigned int lngHzYM2203;
	unsigned int lngHzYM2608;
	unsigned int lngHzYM2610;
	unsigned int lngHzYM3812;
	unsigned int lngHzYM3526;
	unsigned int lngHzY8950;
	unsigned int lngHzYMF262;
	unsigned int lngHzYMF278B;
	unsigned int lngHzYMF271;
	unsigned int lngHzYMZ280B;
	unsigned int lngHzRF5C164;
	unsigned int lngHzPWM;
	unsigned int lngHzAY8910;
	unsigned char bytAYType;
	unsigned char bytAYFlag;
	unsigned char bytAYFlagYM2203;
	unsigned char bytAYFlagYM2608;
	unsigned char bytVolumeModifier;
	unsigned char bytReserved2;
	signed char bytLoopBase;
	unsigned char bytLoopModifier;
} VGM_HEADER;
typedef struct _vgm_gd3_tag
{
	unsigned int fccGD3;
	unsigned int lngVersion;
	unsigned int lngTagLength;
	wchar_t* strTrackNameE;
	wchar_t* strTrackNameJ;
	wchar_t* strGameNameE;
	wchar_t* strGameNameJ;
	wchar_t* strSystemNameE;
	wchar_t* strSystemNameJ;
	wchar_t* strAuthorNameE;
	wchar_t* strAuthorNameJ;
	wchar_t* strReleaseDate;
	wchar_t* strCreator;
	wchar_t* strNotes;
} GD3_TAG;
typedef struct _vgm_pcm_bank_data
{
	unsigned int DataSize;
	unsigned char* Data;
	unsigned int DataStart;
} VGM_PCM_DATA;
typedef struct _vgm_pcm_bank
{
	unsigned int BankCount;
	VGM_PCM_DATA* Bank;
	unsigned int DataSize;
	unsigned int DataPos;
} VGM_PCM_BANK;

#define FCC_VGM	0x206D6756	// 'Vgm '
#define FCC_GD3	0x20336447	// 'Gd3 '
