/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2010  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __IPU_H__
#define __IPU_H__

#include "mpeg2lib/Mpeg.h"
#include "coroutine.h"
#include "IPU_Fifo.h"

// IPU_INLINE_IRQS
// Scheduling ints into the future is a purist approach to emulation, and
// is mostly cosmetic since the emulator itself performs all actions instantly
// (as far as the emulated CPU is concerned).  In some cases this can actually
// cause more sync problems than it supposedly solves, due to accumulated
// delays incurred by the recompiler's low cycle update rate and also Pcsx2
// failing to properly handle pre-emptive DMA/IRQs or cpu exceptions.

// Uncomment the following line to enable inline IRQs for the IPU.  Tests show
// that it doesn't have any effect on compatibility or audio/video sync, and it
// speeds up movie playback by some 6-8%. But it lacks the purist touch, so it's
// not enabled by default.

//#define IPU_INLINE_IRQS

#ifdef _MSC_VER
#pragma pack(1)
#endif


#define ipumsk( src ) ( (src) & 0xff )
#define ipucase( src ) case ipumsk(src)

#ifdef IPU_INLINE_IRQS
#	define IPU_INT_TO( cycles )  ipu1Interrupt()
#	define IPU_INT_FROM( cycles )  ipu0Interrupt()
#	define IPU_FORCEINLINE
#else
#	define IPU_INT_TO( cycles )  if(!(cpuRegs.interrupt & (1<<4))) CPU_INT( DMAC_TO_IPU, cycles )
#	define IPU_INT_FROM( cycles )  CPU_INT( DMAC_FROM_IPU, cycles )
#	define IPU_FORCEINLINE __forceinline
#endif

struct IPUStatus {
	bool InProgress;
	u8 DMAMode;
	bool DMAFinished;
	bool IRQTriggered;
	u8 TagFollow;
	u32 TagAddr;
	bool stalled;
	u8 ChainMode;
	u32 NextMem;
};

static IPUStatus IPU1Status;

#define DMA_MODE_NORMAL 0
#define DMA_MODE_CHAIN 1

#define IPU1_TAG_FOLLOW 0
#define IPU1_TAG_QWC 1
#define IPU1_TAG_ADDR 2
#define IPU1_TAG_NONE 3

//
// Bitfield Structures
//

struct tIPU_CMD
{
	union
	{
		struct
		{
			u32 OPTION : 28;	// VDEC decoded value
			u32 CMD : 4;	// last command
		};
		u32 DATA;
	};
	u32 BUSY;
};

union tIPU_CTRL {
	struct {
		u32 IFC : 4;	// Input FIFO counter
		u32 OFC : 4;	// Output FIFO counter
		u32 CBP : 6;	// Coded block pattern
		u32 ECD : 1;	// Error code pattern
		u32 SCD : 1;	// Start code detected
		u32 IDP : 2;	// Intra DC precision
		u32 resv0 : 2;
		u32 AS : 1;		// Alternate scan
		u32 IVF : 1;	// Intra VLC format
		u32 QST : 1;	// Q scale step
		u32 MP1 : 1;	// MPEG1 bit stream
		u32 PCT : 3;	// Picture Type
		u32 resv1 : 3;
		u32 RST : 1;	// Reset
		u32 BUSY : 1;	// Busy
	};
	u32 _u32;

	tIPU_CTRL( u32 val ) { _u32 = val; }

    // CTRL = the first 16 bits of ctrl [0x8000ffff], + value for the next 16 bits,
    // minus the reserved bits. (18-19; 27-29) [0x47f30000]
	void write(u32 value) { _u32 = (value & 0x47f30000) | (_u32 & 0x8000ffff); }

	bool test(u32 flags) const { return !!(_u32 & flags); }
	void set_flags(u32 flags) { _u32 |= flags; }
	void clear_flags(u32 flags) { _u32 &= ~flags; }
	void reset() { _u32 = 0; }
};

struct tIPU_BP {
	u32 BP;		// Bit stream point
	u16 IFC;	// Input FIFO counter
	u8 FP;		// FIFO point
	u8 bufferhasnew; // Always 0.
	wxString desc() const
	{
		return wxsFormat(L"Ipu BP: bp = 0x%x, IFC = 0x%x, FP = 0x%x.", BP, IFC, FP);
	}
};

#ifdef _WIN32
#pragma pack()
#endif

union tIPU_CMD_IDEC
{
	struct
	{
		u32 FB  : 6;
		u32 UN2 :10;
		u32 QSC : 5;
		u32 UN1 : 3;
		u32 DTD : 1;
		u32 SGN : 1;
		u32 DTE : 1;
		u32 OFM : 1;
		u32 cmd : 4;
	};

	u32 _u32;

	tIPU_CMD_IDEC( u32 val ) { _u32 = val; }

	bool test(u32 flags) const { return !!(_u32 & flags); }
	void set_flags(u32 flags) { _u32 |= flags; }
	void clear_flags(u32 flags) { _u32 &= ~flags; }
	void reset() { _u32 = 0; }
	void log()
	{
		IPU_LOG("IPU IDEC command.");

		if (FB) IPU_LOG(" Skip %d	bits.", FB);
		IPU_LOG(" Quantizer step code=0x%X.", QSC);

		if (DTD == 0)
			IPU_LOG(" Does not decode DT.");
		else
			IPU_LOG(" Decodes DT.");

		if (SGN == 0)
			IPU_LOG(" No bias.");
		else
			IPU_LOG(" Bias=128.");

		if (DTE == 1) IPU_LOG(" Dither Enabled.");
		if (OFM == 0)
			IPU_LOG(" Output format is RGB32.");
		else
			IPU_LOG(" Output format is RGB16.");

		IPU_LOG("");
	}
};

union tIPU_CMD_BDEC
{
	struct
	{
		u32 FB  : 6;
		u32 UN2 :10;
		u32 QSC : 5;
		u32 UN1 : 4;
		u32 DT  : 1;
		u32 DCR : 1;
		u32 MBI : 1;
		u32 cmd : 4;
	};
	u32 _u32;

	tIPU_CMD_BDEC( u32 val ) { _u32 = val; }

	bool test(u32 flags) const { return !!(_u32 & flags); }
	void set_flags(u32 flags) { _u32 |= flags; }
	void clear_flags(u32 flags) { _u32 &= ~flags; }
	void reset() { _u32 = 0; }
	void log(int s_bdec)
	{
		IPU_LOG("IPU BDEC(macroblock decode) command %x, num: 0x%x", cpuRegs.pc, s_bdec);
		if (FB) IPU_LOG(" Skip 0x%X bits.", FB);

		if (MBI)
			IPU_LOG(" Intra MB.");
		else
			IPU_LOG(" Non-intra MB.");

		if (DCR)
			IPU_LOG(" Resets DC prediction value.");
		else
			IPU_LOG(" Doesn't reset DC prediction value.");

		if (DT)
			IPU_LOG(" Use field DCT.");
		else
			IPU_LOG(" Use frame DCT.");

		IPU_LOG(" Quantizer step=0x%X", QSC);
	}
};

union tIPU_CMD_CSC
{
	struct
	{
		u32 MBC :11;
		u32 UN2 :15;
		u32 DTE : 1;
		u32 OFM : 1;
		u32 cmd : 4;
	};
	u32 _u32;

	tIPU_CMD_CSC( u32 val ){ _u32 = val; }

	bool test(u32 flags) const { return !!(_u32 & flags); }
	void set_flags(u32 flags) { _u32 |= flags; }
	void clear_flags(u32 flags) { _u32 &= ~flags; }
	void reset() { _u32 = 0; }
	void log_from_YCbCr()
	{
		IPU_LOG("IPU CSC(Colorspace conversion from YCbCr) command (%d).", MBC);
		if (OFM)
			IPU_LOG("Output format is RGB16. ");
		else
			IPU_LOG("Output format is RGB32. ");

		if (DTE) IPU_LOG("Dithering enabled.");
	}
	void log_from_RGB32()
	{
		IPU_LOG("IPU PACK (Colorspace conversion from RGB32) command.");

		if (OFM)
			IPU_LOG("Output format is RGB16. ");
		else
			IPU_LOG("Output format is INDX4. ");

		if (DTE) IPU_LOG("Dithering enabled.");

		IPU_LOG("Number of macroblocks to be converted: %d", MBC);
	}
};

union tIPU_DMA
{
	struct
	{
		bool GIFSTALL  : 1;
		bool TIE0 :1;
		bool TIE1 : 1;
		bool ACTV1 : 1;
		bool DOTIE1  : 1;
		bool FIREINT0 : 1;
		bool FIREINT1 : 1;
		bool VIFSTALL : 1;
		bool SIFSTALL : 1;
	};
	u32 _u32;

	tIPU_DMA( u32 val ){ _u32 = val; }

	bool test(u32 flags) const { return !!(_u32 & flags); }
	void set_flags(u32 flags) { _u32 |= flags; }
	void clear_flags(u32 flags) { _u32 &= ~flags; }
	void reset() { _u32 = 0; }
	wxString desc() const
	{
		wxString temp(L"g_nDMATransfer[");

		if (GIFSTALL) temp += L" GIFSTALL ";
		if (TIE0) temp += L" TIE0 ";
		if (TIE1) temp += L" TIE1 ";
		if (ACTV1) temp += L" ACTV1 ";
		if (DOTIE1) temp += L" DOTIE1 ";
		if (FIREINT0) temp += L" FIREINT0 ";
		if (FIREINT1) temp += L" FIREINT1 ";
		if (VIFSTALL) temp += L" VIFSTALL ";
		if (SIFSTALL) temp += L" SIFSTALL ";

		temp += L"]";
		return temp;
	}
};

enum SCE_IPU
{
	SCE_IPU_BCLR = 0x0
,	SCE_IPU_IDEC
,	SCE_IPU_BDEC
,	SCE_IPU_VDEC
,	SCE_IPU_FDEC
,	SCE_IPU_SETIQ
,	SCE_IPU_SETVQ
,	SCE_IPU_CSC
,	SCE_IPU_PACK
,	SCE_IPU_SETTH
};

struct IPUregisters {
  tIPU_CMD  cmd;
  u32 dummy0[2];
  tIPU_CTRL ctrl;
  u32 dummy1[3];
  u32   ipubp;
  u32 dummy2[3];
  u32		top;
  u32		topbusy;
  u32 dummy3[2];
};

#define ipuRegs ((IPUregisters*)(PS2MEM_HW+0x2000))

struct tIPU_cmd
{
	int index;
	int pos[2];
	int current;
	void clear()
	{
		memzero(pos);
		index = 0;
		current = 0xffffffff;
	}
	wxString desc() const
	{
		return wxsFormat(L"Ipu cmd: index = 0x%x, current = 0x%x, pos[0] = 0x%x, pos[1] = 0x%x",
			index, current, pos[0], pos[1]);
	}
};

//extern tIPU_cmd ipu_cmd;
extern tIPU_BP g_BP;
extern int coded_block_pattern;
extern int g_nIPU0Data; // or 0x80000000 whenever transferring
extern u8* g_pIPU0Pointer;

// The IPU can only do one task at once and never uses other buffers so these
// should be made available to functions in other modules to save registers.
extern __aligned16 macroblock_rgb32	rgb32;
extern __aligned16 macroblock_8		mb8;

extern int ipuInit();
extern void ipuReset();
extern void ipuShutdown();
extern int  ipuFreeze(gzFile f, int Mode);
extern bool ipuCanFreeze();

extern u32 ipuRead32(u32 mem);
extern u64 ipuRead64(u32 mem);
extern void ipuWrite32(u32 mem,u32 value);
extern void ipuWrite64(u32 mem,u64 value);

extern void IPUCMD_WRITE(u32 val);
extern void ipuSoftReset();
extern void IPUProcessInterrupt();
extern void ipu0Interrupt();
extern void ipu1Interrupt();

extern void dmaIPU0();
extern void dmaIPU1();
extern int IPU0dma();
extern int IPU1dma();

extern u16 __fastcall FillInternalBuffer(u32 * pointer, u32 advance, u32 size);
extern u8 __fastcall getBits32(u8 *address, u32 advance);
extern u8 __fastcall getBits16(u8 *address, u32 advance);
extern u8 __fastcall getBits8(u8 *address, u32 advance);
extern int __fastcall getBits(u8 *address, u32 size, u32 advance);


#endif
