/*
 *  CBM 1530/1531 tape routines.
 *  Copyright 2012 Arnd Menge, arnd(at)jonnz(dot)de
*/

#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>

#include <arch.h>
#include "cap.h"
#include "tap-cbm.h"
#include "misc.h"

#define EXTERN __declspec(dllexport) /*!< we are exporting the functions */
#define CBMAPIDECL __cdecl

#define FREQ_C64_PAL    985248
#define FREQ_C64_NTSC  1022727
#define FREQ_VIC_PAL   1108405
#define FREQ_VIC_NTSC  1022727
#define FREQ_C16_PAL    886724
#define FREQ_C16_NTSC   894886

#define NeedEvenSplitNumber  0
#define NeedOddSplitNumber   1
#define NeedSplit            2
#define NoSplit              3

// Define pulses.
#define ShortPulse 1
#define LongPulse  2
#define PausePulse 3

// Define waveforms.
#define HalfWave   0
#define ShortWave  1
#define LongWave   2
#define PauseWave  3
#define ErrorWave  4

// Global variables
unsigned __int8   CAP_Machine, CAP_Video, CAP_StartEdge, CAP_SignalFormat;
unsigned __int32  CAP_Precision, CAP_SignalWidth, CAP_StartOfs, CAP_FileSize;
unsigned __int8   TAP_Machine, TAP_Video, TAPv;
unsigned __int32  TAP_ByteCount;

typedef void (*tap2cap_convert_callback_t)(unsigned __int32 TapCounter, unsigned __int32 TapByteCount);
typedef void (*cap2tap_convert_callback_t)(int CapCounter, int CapFileSize);

// Convert CAP to Spectrum48K TAP format. *EXPERIMENTAL*
__int32 CAP2SPEC48KTAP(HANDLE hCAP, FILE *TapFile)
{
    unsigned __int8  DBGFLAG = 0; // 1 = Debug output
    unsigned __int8  *zb; // Spectrum48K TAP image buffer.
    unsigned __int8  ch = 0;
    unsigned __int64 ui64Delta, ui64Len;
    unsigned __int32 Timer_Precision_MHz;
    __int32          FuncRes;    // Function call result.
    __int32          RetVal = 0; // Default return value.

    // Declare variables holding current/last pulse & wave information.
    unsigned __int8 LastPulse = PausePulse;
    unsigned __int8 Pulse     = PausePulse;
    unsigned __int8 Wave      = HalfWave;

    // Declare image pointers.
    unsigned __int32 BlockStart = 0; // Data block start in image.
    unsigned __int32 BlockPos   = 2; // Current position in image.

    // Declare bit & byte counters.
    unsigned __int8  BitCount         = 0; // Data block bit counter.
    unsigned __int32 ByteCount        = 0; // Data block data byte counter.
    unsigned __int32 DataPulseCounter = 0; // Data block pulse counter.
    unsigned __int32 BlockByteCounter = 0; // Data block byte counter.

    // Get memory for Spectrum48K TAP image buffer. Should be dynamic size.
    zb = (unsigned __int8 *)malloc((size_t)10000000);
    if (zb == NULL)
    {
        printf("Error: Not enough memory for Spectrum48K TAP image buffer.");
        return -1;
    }
    memset((void *)zb, (__int32)0, (size_t)10000000);

    // Seek to start of image file and read image header, extract & verify header contents, seek to start of image data.
    FuncRes = CAP_ReadHeader(hCAP);
    if (FuncRes != CAP_Status_OK)
    {
        CAP_OutputError(FuncRes);
        RetVal = -1;
        goto exit;
    }

    // Return timestamp precision from header.
    FuncRes = CAP_GetHeader_Precision(hCAP, &Timer_Precision_MHz);
    if (FuncRes != CAP_Status_OK)
    {
        CAP_OutputError(FuncRes);
        RetVal = -1;
        goto exit;
    }

    // Skip first halfwave (time until first pulse occurs).
    FuncRes = CAP_ReadSignal(hCAP, &ui64Delta, NULL);
    if (FuncRes == CAP_Status_OK_End_of_file)
    {
        printf("Error: Empty image file.");
        RetVal = -1;
        goto exit;
    }
    else if (FuncRes == CAP_Status_Error_Reading_data)
    {
        CAP_OutputError(FuncRes);
        RetVal = -1;
        goto exit;
    }

    // While CAP 5-byte timestamp available.
    while ((FuncRes = CAP_ReadSignal(hCAP, &ui64Delta, NULL)) == CAP_Status_OK)
    {
        ui64Len = (ui64Delta+(Timer_Precision_MHz/2))/Timer_Precision_MHz;

        if (DBGFLAG == 1) printf("%I64u ", ui64Len);

        LastPulse = Pulse;

        // Evaluate current pulse width.
        if ((150 <= ui64Len) && (ui64Len <= 360))
        {
            Pulse = ShortPulse;
            if (DBGFLAG == 1) printf("(SP) ");
        }
        else if ((360 < ui64Len) && (ui64Len < 550))
        {
            Pulse = LongPulse;
            if (DBGFLAG == 1) printf("(LP) ");
        }
        else // <150 or >550
        {
            Pulse = PausePulse;
            if (DBGFLAG == 1) printf("(PP) ");
        }


        if (Pulse == PausePulse)
        {
            DataPulseCounter = 0;
            BlockByteCounter = 0;

            if (ByteCount > 0)
            {
                // Calculate block size and write to TAP image.
                zb[BlockStart  ] = ByteCount & 0xff;
                zb[BlockStart+1] = (ByteCount >> 8) & 0xff;
                if (DBGFLAG == 1) printf("Block size = %u", ByteCount);
                BlockStart = BlockPos;
                BlockPos += 2;
            }
            ByteCount = 0;
            BitCount = 0;

        }
        else DataPulseCounter++;


        // Evaluate waveform after every second data pulse.
        if ((DataPulseCounter > 0) && ((DataPulseCounter % 2) == 0))
        {

            if ((LastPulse == ShortPulse) && (Pulse == ShortPulse))
            {
                Wave = ShortWave;
                if (DBGFLAG == 1) printf("(SW) ");
            }
            else if ((LastPulse == LongPulse) && (Pulse == LongPulse))
            {
                Wave = LongWave;
                if (DBGFLAG == 1) printf("(LW) ");
            }
            else
            {
                Wave = ErrorWave;
                if (DBGFLAG == 1) printf("(EW) ");
            }

            if ((Wave == ShortWave) || (Wave == LongWave))
                BlockByteCounter++;
            else
                BlockByteCounter = 0;


            if (BlockByteCounter > 1)
            {
                // We found a bit.
                BitCount++;

                // Evaluate wave.
                if (Wave == ShortWave)
                {
                    ch = (ch << 1);
                    if (DBGFLAG == 1) printf("(0)");
                }
                else if (Wave == LongWave)
                {
                    ch = (ch << 1) + 1;
                    if (DBGFLAG == 1) printf("(1)");
                }

                if (BitCount == 8)
                {
                    ByteCount++; // Increase byte counter.
                    BitCount = 0; // Reset bit counter.

                    zb[BlockPos++] = ch; // Store byte to image.
                    if (DBGFLAG == 1) printf(" -----> 0x%.2x <%c>", ch, ch);

                    if (ByteCount == 1)
                    {
                        // Evaluate first block byte.
                        if (DBGFLAG == 1)
                        {
                            if (ch == 0)
                                printf(" [Header]");
                            else if (ch == 0xff)
                                printf(" [Data]");
                            else
                                printf(" [Unknown block!]");
                        }
                    }
                } // if (BitCount == 8)

            } // if (BlockCounter > 1)
            else if (DBGFLAG == 1) printf("(x)");
        } // if ((DataPulseCounter > 0) && ((DataPulseCounter % 2) == 0))
    } // While CAP 5-byte timestamp available.

    if (FuncRes == CAP_Status_Error_Reading_data)
    {
        CAP_OutputError(FuncRes);
        RetVal = -1;
        goto exit;
    }

    // Handle final data block, if exists.
    if (ByteCount > 0)
    {
        // Calculate block size and write to TAP image.
        zb[BlockStart  ] = ByteCount & 0xff;
        zb[BlockStart+1] = (ByteCount >> 8) & 0xff;
        if (DBGFLAG == 1) printf("Block size = %u\n", ByteCount);
        BlockStart = BlockPos;
        BlockPos += 2;
    }

    if (BlockPos > 2)
        fwrite((const void *)zb, (size_t)(BlockPos-2), (size_t)1, (FILE *)TapFile);
    else
    {
        printf("Error: Empty image file.\n");
        RetVal = -1;
        goto exit;
    }

exit:
    // Release memory for Spectrum48K TAP image buffer.
    free((void *)zb);

    return RetVal;
}


__int32 HandlePause(HANDLE hTAP, unsigned __int64 ui64Len, unsigned __int8 uiNeededSplit, unsigned __int8 TAPv, unsigned __int32 *puiCounter)
{
    unsigned __int32 numsplits, i;
    __int32          FuncRes;

    if (TAPv == TAPv2)
    {
        if (ui64Len > 0x00ffffff)
        {
            numsplits = (unsigned __int32) (ui64Len/0x00ffffff);
            if ((ui64Len % 0x00ffffff) != 0) numsplits++;

            if (((unsigned __int8)(numsplits % 2)) != uiNeededSplit) // NeedEvenSplitNumber=0 / NeedOddSplitNumber=1
            {
                // Split last 2 parts into 3, make sure last halfwave is not too short.
                // Write (n-2) long ones, last two /3: 0.33 < length < 0.67.
                //             even     odd      even
                // |        |        |        |       *|
                // |        |        |        |*       |
                // |        |        |       *|        |
                // |        |        |*       |        |
                // |        |       *|        |        |
                // |        |*       |        |        |

                // First two 2/3-fractions, last 1/3 remaining.
                for (i=1; i<=2; i++)
                {
                    // Write 32bit unsigned integer to image file: LSB first, MSB last.
                    Check_TAP_CBM_Error_TextRetM1(TAP_CBM_WriteSignal_4Bytes(hTAP, 0x55555500, puiCounter));
                    ui64Len -= 0x00555555;
                }
            }
            else if ((ui64Len % 0x00ffffff) < 0x007fffff)
            {
                // Make sure last halfwave is not too short: Pull 0x007fffff.
                // Does not change numsplits.
                // Write 32bit unsigned integer to image file: LSB first, MSB last.
                Check_TAP_CBM_Error_TextRetM1(TAP_CBM_WriteSignal_4Bytes(hTAP, 0x7fffff00, puiCounter));
                ui64Len -= 0x007fffff;
            }
        }
    }

    if ((TAPv == TAPv1) || (TAPv == TAPv2))
    {
        while (ui64Len > 0x00ffffff)
        {
            // Write 32bit unsigned integer to image file: LSB first, MSB last.
            Check_TAP_CBM_Error_TextRetM1(TAP_CBM_WriteSignal_4Bytes(hTAP, 0xffffff00, puiCounter));
            ui64Len -= 0x00ffffff;
        }
        if (ui64Len > 0)
        {
            // Write 32bit unsigned integer to image file: LSB first, MSB last.
            Check_TAP_CBM_Error_TextRetM1(TAP_CBM_WriteSignal_4Bytes(hTAP, (unsigned __int32) ((ui64Len << 8) & 0xffffff00), puiCounter));
        }
    }

    if (TAPv == TAPv0)
    {
        while (ui64Len > 2040)
        {
            Check_TAP_CBM_Error_TextRetM1(TAP_CBM_WriteSignal_1Byte(hTAP, 0, puiCounter));
            ui64Len -= 2040;
        }
        if (ui64Len > 0)
        {
            Check_TAP_CBM_Error_TextRetM1(TAP_CBM_WriteSignal_1Byte(hTAP, 0, puiCounter));
            ui64Len = 0;
        }
    }

    return 0;
}


__int32 Initialize_TAP_header_and_return_frequencies(HANDLE hCAP, HANDLE hTAP, unsigned __int32 *puiTimer_Precision_MHz, unsigned __int32 *puiFreq)
{
    unsigned __int8 CAP_Machine, CAP_Video, TAP_Machine, TAP_Video, TAP_Version;
    __int32         FuncRes;

    // Seek to start of image file and read image header, extract & verify header contents, seek to start of image data.
    FuncRes = CAP_ReadHeader(hCAP);
    if (FuncRes != CAP_Status_OK)
    {
        CAP_OutputError(FuncRes);
        return -1;
    }

    // Return target machine type from header.
    FuncRes = CAP_GetHeader_Machine(hCAP, &CAP_Machine);
    if (FuncRes != CAP_Status_OK)
    {
        CAP_OutputError(FuncRes);
        return -1;
    }

    // Return target video type from header.
    FuncRes = CAP_GetHeader_Video(hCAP, &CAP_Video);
    if (FuncRes != CAP_Status_OK)
    {
        CAP_OutputError(FuncRes);
        return -1;
    }

    if (CAP_Machine == CAP_Machine_C64)
    {
        printf("* C64\n");
        TAP_Machine = TAP_Machine_C64;
        TAP_Version = TAPv1;
    }
    else if (CAP_Machine == CAP_Machine_C16)
    {
        printf("* C16\n");
        TAP_Machine = TAP_Machine_C16;
        TAP_Version = TAPv2;
    }
    else if (CAP_Machine == CAP_Machine_VC20)
    {
        printf("* VC20\n");
        TAP_Machine = TAP_Machine_C64;
        TAP_Version = TAPv1;
    }
    else return -1;

    if (CAP_Video == CAP_Video_PAL)
    {
        printf("* PAL\n");
        TAP_Video = TAP_Video_PAL;
    }
    else if (CAP_Video == CAP_Video_NTSC)
    {
        printf("* NTSC\n");
        TAP_Video = TAP_Video_NTSC;
    }
    else return -1;

    // Set all header entries at once.
    FuncRes = TAP_CBM_SetHeader(hTAP, TAP_Machine, TAP_Video, TAP_Version, 0);
    if (FuncRes != TAP_CBM_Status_OK)
    {
        TAP_CBM_OutputError(FuncRes);
        return -1;
    }

    // Seek to start of file & write image header.
    FuncRes = TAP_CBM_WriteHeader(hTAP);
    if (FuncRes != TAP_CBM_Status_OK)
    {
        TAP_CBM_OutputError(FuncRes);
        return -1;
    }

    // Determine frequencies.

    // Return timestamp precision from header.
    FuncRes = CAP_GetHeader_Precision(hCAP, puiTimer_Precision_MHz);
    if (FuncRes != CAP_Status_OK)
    {
        CAP_OutputError(FuncRes);
        return -1;
    }

    if (     (CAP_Machine == CAP_Machine_C64)  && (CAP_Video == CAP_Video_PAL))
        *puiFreq = FREQ_C64_PAL;
    else if ((CAP_Machine == CAP_Machine_C64)  && (CAP_Video == CAP_Video_NTSC))
        *puiFreq = FREQ_C64_NTSC;
    else if ((CAP_Machine == CAP_Machine_VC20) && (CAP_Video == CAP_Video_PAL))
        *puiFreq = FREQ_VIC_PAL;
    else if ((CAP_Machine == CAP_Machine_VC20) && (CAP_Video == CAP_Video_NTSC))
        *puiFreq = FREQ_VIC_NTSC;
    else if ((CAP_Machine == CAP_Machine_C16)  && (CAP_Video == CAP_Video_PAL))
        *puiFreq = FREQ_C16_PAL;
    else if ((CAP_Machine == CAP_Machine_C16)  && (CAP_Video == CAP_Video_NTSC))
        *puiFreq = FREQ_C16_NTSC;
    else
    {
        printf("Error: Can't determine machine frequency.\n");
        return -1;
    }

    printf("\n");

    return 0;
}


__int32 HandleDeltaAndWriteToCAP(HANDLE hCAP, unsigned __int64 ui64Delta, unsigned __int8 uiSplit)
{
	unsigned __int64 ui64SplitLen;
	__int32          FuncRes;

	if (uiSplit == NeedSplit)
	{
		// Write two halfwaves.
		ui64SplitLen = ui64Delta/2;
		Check_CAP_Error_TextRetM1(CAP_WriteSignal(hCAP, ui64SplitLen, NULL));
		ui64SplitLen = ui64Delta-ui64SplitLen;
		Check_CAP_Error_TextRetM1(CAP_WriteSignal(hCAP, ui64SplitLen, NULL));
	}
	else
	{
		// Write one halfwave.
		Check_CAP_Error_TextRetM1(CAP_WriteSignal(hCAP, ui64Delta, NULL));
	}

	return 0;
}


__int32 Initialize_CAP_header_and_return_frequency(HANDLE hCAP, HANDLE hTAP, unsigned __int32 *puiFreq)
{
	__int32 FuncRes;

	// Determine target machine & video.

	if (TAP_Machine == TAP_Machine_C64)
	{
		printf("* C64\n");
		CAP_Machine = CAP_Machine_C64;
	}
	else if (TAP_Machine == TAP_Machine_C16)
	{
		printf("* C16\n");
		CAP_Machine = CAP_Machine_C16;
	}
	else if (TAP_Machine == TAP_Machine_VC20)
	{
		printf("* VC20\n");
		CAP_Machine = CAP_Machine_VC20;
	}
	else return -1;

	if (TAP_Video == TAP_Video_PAL)
	{
		printf("* PAL\n");
		CAP_Video = CAP_Video_PAL;
	}
	else if (TAP_Video == TAP_Video_NTSC)
	{
		printf("* NTSC\n");
		CAP_Video = CAP_Video_NTSC;
	}
	else return -1;

	// Initialize & write CAP image header.

	CAP_Precision    = 1;                         // Default: 1us signal precision.
	CAP_StartEdge    = CAP_StartEdge_Falling;     // Default: Start with falling signal edge.
	CAP_SignalFormat = CAP_SignalFormat_Relative; // Default: Relative timings instead of absolute.
	CAP_SignalWidth  = CAP_SignalWidth_40bit;     // Default: 40bit.
	CAP_StartOfs     = CAP_Default_Data_Start_Offset+0x30; // Text addon after standard header.

	FuncRes = CAP_SetHeader(hCAP, CAP_Precision, CAP_Machine, CAP_Video, CAP_StartEdge, CAP_SignalFormat, CAP_SignalWidth, CAP_StartOfs);
	if (FuncRes != CAP_Status_OK)
	{
		CAP_OutputError(FuncRes);
		return -1;
	}

	FuncRes = CAP_WriteHeader(hCAP);
	if (FuncRes != CAP_Status_OK)
	{
		CAP_OutputError(FuncRes);
		return -1;
	}

	FuncRes = CAP_WriteHeaderAddon(hCAP, "   Created by       TAP2CAP     ----------------", 0x30);
	if (FuncRes != CAP_Status_OK)
	{
		CAP_OutputError(FuncRes);
		return -1;
	}

	// Determine frequency.

	if (     (TAP_Machine == TAP_Machine_C64)  && (TAP_Video == TAP_Video_PAL))
		*puiFreq = FREQ_C64_PAL;
	else if ((TAP_Machine == TAP_Machine_C64)  && (TAP_Video == TAP_Video_NTSC))
		*puiFreq = FREQ_C64_NTSC;
	else if ((TAP_Machine == TAP_Machine_VC20) && (TAP_Video == TAP_Video_PAL))
		*puiFreq = FREQ_VIC_PAL;
	else if ((TAP_Machine == TAP_Machine_VC20) && (TAP_Video == TAP_Video_NTSC))
		*puiFreq = FREQ_VIC_NTSC;
	else if ((TAP_Machine == TAP_Machine_C16)  && (TAP_Video == TAP_Video_PAL))
		*puiFreq = FREQ_C16_PAL;
	else if ((TAP_Machine == TAP_Machine_C16)  && (TAP_Video == TAP_Video_NTSC))
		*puiFreq = FREQ_C16_NTSC;
	else
	{
		printf("Error: Can't determine machine frequency.\n");
		return -1;
	}

	printf("\n");

	return 0;
}


// Convert CBM TAP to CAP format.
__int32 CBMTAP2CAP(HANDLE hCAP, HANDLE hTAP, tap2cap_convert_callback_t ConvertCallback)
{
	unsigned __int64 ui64Delta;
	unsigned __int32 uiDelta, uiFreq;
	unsigned __int32 TAP_Counter = 0; // CAP & TAP file byte counters.
	unsigned __int8  ch;
	__int32          FuncRes;

	// Seek to & read image header, extract & verify header contents.
	FuncRes = TAP_CBM_ReadHeader(hTAP);
	if (FuncRes != TAP_CBM_Status_OK)
	{
		TAP_CBM_OutputError(FuncRes);
		return -1;
	}

	// Get all header entries at once. 
	FuncRes = TAP_CBM_GetHeader(hTAP, &TAP_Machine, &TAP_Video, &TAPv, &TAP_ByteCount);
	if (FuncRes != TAP_CBM_Status_OK)
	{
		TAP_CBM_OutputError(FuncRes);
		return -1;
	}
	if (ConvertCallback != NULL)
		ConvertCallback(0, TAP_ByteCount);

	if (Initialize_CAP_header_and_return_frequency(hCAP, hTAP, &uiFreq) != 0)
		return -1;

	// Start conversion TAP->CAP.

	// Start with 100us delay (can be replaced with specified start delay in tapwrite).
	ui64Delta = CAP_Precision*100;
	if (HandleDeltaAndWriteToCAP(hCAP, ui64Delta, NoSplit) == -1)
		return -1;

	// Conversion loop.
	while ((FuncRes = TAP_CBM_ReadSignal(hTAP, &uiDelta, &TAP_Counter)) == TAP_CBM_Status_OK)
	{
		ui64Delta = uiDelta;
		ui64Delta = (ui64Delta*1000000*CAP_Precision+uiFreq/2)/uiFreq;    

		if ((TAPv == TAPv0) || (TAPv == TAPv1))
		{
			// Generate two halfwaves.
			if (HandleDeltaAndWriteToCAP(hCAP, ui64Delta, NeedSplit) == -1)
				return -1;
		}
		else
		{
			// Generate one halfwave.
			if (HandleDeltaAndWriteToCAP(hCAP, ui64Delta, NoSplit) == -1)
				return -1;
		}
    
		if (ConvertCallback != NULL)
			ConvertCallback(TAP_Counter, TAP_ByteCount);
	}

	if (FuncRes == TAP_CBM_Status_Error_Reading_data)
	{
		TAP_CBM_OutputError(FuncRes);
		return -1;
	}

	return 0;
}


// Convert CAP to CBM TAP format.
__int32 CAP2CBMTAP(HANDLE hCAP, HANDLE hTAP, cap2tap_convert_callback_t ConvertCallback)
{
    unsigned __int64 ui64Delta, ui64Delta2, ui64Len;
    int CAP_Counter = 0; // CAP file byte counters.
    unsigned __int32 Timer_Precision_MHz, uiFreq;
    unsigned __int8  TAPv; // TAP file format version.
    unsigned __int8  ch;   // Single TAP data byte.
    __int32          TAP_Counter = 0; // CAP & TAP file byte counters.
    __int32          FuncRes, ReadFuncRes; // Function call results.

    if (Initialize_TAP_header_and_return_frequencies(hCAP, hTAP, &Timer_Precision_MHz, &uiFreq) != 0)
        return -1;

    // Start conversion CAP->TAP.

    // Get target TAP version from header.
    FuncRes = TAP_CBM_GetHeader_TAPversion(hTAP, &TAPv);
    if (FuncRes != TAP_CBM_Status_OK)
    {
        TAP_CBM_OutputError(FuncRes);
        return -1;
    }
    
	if (ConvertCallback != NULL)
		ConvertCallback(CAP_Counter, CAP_FileSize);    

    // Skip first halfwave (time until first pulse starts).
    FuncRes = CAP_ReadSignal(hCAP, &ui64Delta, &CAP_Counter);
    if (FuncRes == CAP_Status_OK_End_of_file)
    {
        printf("Error: Empty image file.");
        return -1;
    }
    else if (FuncRes == CAP_Status_Error_Reading_data)
    {
        CAP_OutputError(FuncRes);
        return -1;
    }

    // Convert while CAP file signal available.
    while ((ReadFuncRes = CAP_ReadSignal(hCAP, &ui64Delta, &CAP_Counter)) == CAP_Status_OK)
    {
        if ((TAPv == TAPv0) || (TAPv == TAPv1))
        {
            // Get and add timestamp of falling edge.
            FuncRes = CAP_ReadSignal(hCAP, &ui64Delta2, &CAP_Counter);
            if (FuncRes == CAP_Status_Error_Reading_data)
            {
                CAP_OutputError(FuncRes);
                return -1;
            }
            else if (FuncRes == CAP_Status_OK_End_of_file)
                break;

            ui64Delta += ui64Delta2;
        }

        ui64Len = (ui64Delta*uiFreq/Timer_Precision_MHz+500000)/1000000;

        if (ui64Len > 2040) // 8*0xff=2040
        {
            // We have a pause.
            if ((TAPv == TAPv0) || (TAPv == TAPv1))
            {
                if (HandlePause(hTAP, ui64Len, NeedEvenSplitNumber, TAPv, &TAP_Counter) == -1)
                    return -1;
            }
            else
            {
                if (HandlePause(hTAP, ui64Len, NeedOddSplitNumber, TAPv, &TAP_Counter) == -1)
                    return -1;
            }
        }
        else
        {
            // We have a data byte.
            ch = (unsigned __int8) ((ui64Len+4)/8);
            Check_TAP_CBM_Error_TextRetM1(TAP_CBM_WriteSignal_1Byte(hTAP, ch, &TAP_Counter));
        }
        
        if (ConvertCallback != NULL)
            ConvertCallback(CAP_Counter, CAP_FileSize);        
    } // Convert while CAP file signal available.

    if (ReadFuncRes == CAP_Status_Error_Reading_data)
    {
        CAP_OutputError(ReadFuncRes);
        return -1;
    }

    // Set signal byte count in header (sum of all signal bytes).
    FuncRes = TAP_CBM_SetHeader_ByteCount(hTAP, TAP_Counter);
    if (FuncRes != TAP_CBM_Status_OK)
    {
        TAP_CBM_OutputError(FuncRes);
        return -1;
    }

    // Seek to start of file & write image header.
    FuncRes = TAP_CBM_WriteHeader(hTAP);
    if (FuncRes != TAP_CBM_Status_OK)
    {
        TAP_CBM_OutputError(FuncRes);
        return -1;
    }

    return 0;
}


EXTERN int CBMAPIDECL tap_file_to_cap_file(unsigned char *tap_file_name, unsigned char *cap_file_name, tap2cap_convert_callback_t ConvertCallback)
{
	HANDLE  hCAP, hTAP;
	__int32 FuncRes, RetVal = -1;

	// Open specified TAP image file for reading.
	FuncRes = TAP_CBM_OpenFile(&hTAP, tap_file_name);
	if (FuncRes != TAP_CBM_Status_OK)
	{
		TAP_CBM_OutputError(FuncRes);
		goto exit;
	}

	// Create specified CAP image file for writing.
	FuncRes = CAP_CreateFile(&hCAP, cap_file_name);
	if (FuncRes != CAP_Status_OK)
	{
		RetVal = FuncRes;
		CAP_OutputError(FuncRes);
		TAP_CBM_CloseFile(&hTAP);
		goto exit;
	}

	// Convert CBM TAP to CAP format.
	RetVal = CBMTAP2CAP(hCAP, hTAP, ConvertCallback);

	TAP_CBM_CloseFile(&hTAP);

	FuncRes = CAP_CloseFile(&hCAP);
	if (FuncRes != CAP_Status_OK)
	{
		RetVal = FuncRes;
		CAP_OutputError(FuncRes);
	}  

    exit:
   	return RetVal;
}

EXTERN int CBMAPIDECL cap_file_to_tap_file(unsigned char *cap_file_name, unsigned char *tap_file_name, cap2tap_convert_callback_t ConvertCallback)
{
    HANDLE          hCAP, hTAP;
    FILE            *fd; // Experimental Spectrum48K support.
    unsigned __int8 CAP_Machine;
    __int32         FuncRes, RetVal = -1;

    // Open specified image file for reading.
    FuncRes = CAP_OpenFile(&hCAP, cap_file_name);
    if (FuncRes != CAP_Status_OK)
    {
        CAP_OutputError(FuncRes);
        goto exit;
    }
    
    // Read CAP file size
    FuncRes = CAP_GetFileSize(hCAP, &CAP_FileSize);
    if (FuncRes != CAP_Status_OK)
    {
        CAP_OutputError(FuncRes);
        CAP_CloseFile(&hCAP);
        goto exit;
    }
    
    // Seek to start of image file and read image header, extract & verify header contents, seek to start of image data.
    FuncRes = CAP_ReadHeader(hCAP);
    if (FuncRes != CAP_Status_OK)
    {
        CAP_OutputError(FuncRes);
        CAP_CloseFile(&hCAP);
        goto exit;
    }

    // Get target machine type from header.
    FuncRes = CAP_GetHeader_Machine(hCAP, &CAP_Machine);
    if (FuncRes != CAP_Status_OK)
    {
        CAP_OutputError(FuncRes);
        CAP_CloseFile(&hCAP);
        goto exit;
    }

    // Create specified TAP image file for writing.
    if (CAP_Machine == CAP_Machine_Spec48K)
    {
        // Spectrum48K support is *EXPERIMENTAL*
        fd = fopen(tap_file_name, "wb");
        if (fd == NULL)
        {
            printf("Error creating TAP file.");
            CAP_CloseFile(&hCAP);
            goto exit;
        }
    }
    else
    {
        FuncRes = TAP_CBM_CreateFile(&hTAP, tap_file_name);
        if (FuncRes != CAP_Status_OK)
        {
            CAP_OutputError(FuncRes);
            CAP_CloseFile(&hCAP);
            goto exit;
        }
    }

    if (CAP_Machine == CAP_Machine_Spec48K)
    {
        // Convert CAP to Spectrum48K TAP format. *EXPERIMENTAL*
        RetVal = CAP2SPEC48KTAP(hCAP, fd);

        CAP_CloseFile(&hCAP);

        if (fclose(fd) != 0)
        {
            printf("Error: Closing TAP file failed.");
            RetVal = -1;
        }
    }
    else
    {
        // Convert CAP to CBM TAP format.
        RetVal = CAP2CBMTAP(hCAP, hTAP, ConvertCallback);

        CAP_CloseFile(&hCAP);

        FuncRes = TAP_CBM_CloseFile(&hTAP);
        if (FuncRes != TAP_CBM_Status_OK)
        {
            TAP_CBM_OutputError(FuncRes);
            RetVal = -1;
        }
    }

    if (RetVal == 0)
        printf("Conversion successful.");

    exit:
    printf("\n");
    return RetVal;
}


