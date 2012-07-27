/*************************************************************************
** Copyright (c) 2011-2012 Accusoft                                     **
** This program is free software, licensed under the GPLv3              **
** see README.license for details                                       **
**									**
** For obtaining other licenses, contact the author at                  **
** thor@math.tu-berlin.de                                               **
**                                                                      **
** Written by Thomas Richter (THOR Software)                            **
** Sponsored by Accusoft, Tampa, FL and					**
** the Computing Center of the University of Stuttgart                  **
**************************************************************************

This software is a complete implementation of ITU T.81 - ISO/IEC 10918,
also known as JPEG. It implements the standard in all its variations,
including lossless coding, hierarchical coding, arithmetic coding and
DNL, restart markers and 12bpp coding.

In addition, it includes support for new proposed JPEG technologies that
are currently under discussion in the SC29/WG1 standardization group of
the ISO (also known as JPEG). These technologies include lossless coding
of JPEG backwards compatible to the DCT process, and various other
extensions.

The author is a long-term member of the JPEG committee and it is hoped that
this implementation will trigger and facilitate the future development of
the JPEG standard, both for private use, industrial applications and within
the committee itself.

  Copyright (C) 2011-2012 Accusoft, Thomas Richter <thor@math.tu-berlin.de>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*************************************************************************/
/*
**
** Represents the scan including the scan header.
**
** $Id: acdifferentialsequentialscan.cpp,v 1.4 2012-06-02 10:27:13 thor Exp $
**
*/

/// Includes
#include "codestream/acdifferentialsequentialscan.hpp"
#include "codestream/tables.hpp"
#include "marker/frame.hpp"
#include "marker/scan.hpp"
#include "marker/component.hpp"
#include "coding/quantizedrow.hpp"
#include "codestream/rectanglerequest.hpp"
#include "dct/idct.hpp"
#include "dct/fdct.hpp"
#include "dct/sermsdct.hpp"
#include "std/assert.hpp"
#include "interface/bitmaphook.hpp"
#include "interface/imagebitmap.hpp"
#include "colortrafo/colortrafo.hpp"
#include "tools/traits.hpp"
#include "control/blockbitmaprequester.hpp"
#include "control/blockbuffer.hpp"
#include "control/blocklineadapter.hpp"
#include "coding/actemplate.hpp"
#include "marker/actable.hpp"
///

/// ACDifferentialSequentialScan::ACDifferentialSequentialScan
ACDifferentialSequentialScan::ACDifferentialSequentialScan(class Frame *frame,class Scan *scan,
				   UBYTE start,UBYTE stop,UBYTE lowbit)
  : EntropyParser(frame,scan), m_pBlockCtrl(NULL),
    m_ucScanStart(start), m_ucScanStop(stop), m_ucLowBit(lowbit)
{
  m_ucCount = scan->ComponentsInScan();
  
  for(UBYTE i = 0;i < m_ucCount;i++) {
    m_ucSmall[i]     = 0;
    m_ucLarge[i]     = 1;
    m_ucBlockEnd[i]  = 5;
  }
}
///

/// ACDifferentialSequentialScan::~ACDifferentialSequentialScan
ACDifferentialSequentialScan::~ACDifferentialSequentialScan(void)
{
}
///

/// ACDifferentialSequentialScan::StartParseScan
void ACDifferentialSequentialScan::StartParseScan(class ByteStream *io,class BufferCtrl *ctrl)
{ 
  class ACTemplate *ac,*dc;
  int i;

  for(i = 0;i < m_ucCount;i++) {
    dc = m_pScan->DCConditionerOf(i);
    ac = m_pScan->ACConditionerOf(i);

    if (dc) {
      m_ucSmall[i]    = dc->LowerThresholdOf();
      m_ucLarge[i]    = dc->UpperThresholdOf();
    } else {
      m_ucSmall[i]    = 0;
      m_ucLarge[i]    = 1;
    }

    if (ac) {
      m_ucBlockEnd[i] = ac->BandDiscriminatorOf();
    } else {
      m_ucBlockEnd[i] = 5;
    }

    m_lDiff[i]       = 0;
    m_ulX[i]         = 0;
  }
  m_Context.Init();
  
  assert(!ctrl->isLineBased());
  m_pBlockCtrl = dynamic_cast<BlockBuffer *>(ctrl);
  m_pBlockCtrl->ResetToStartOfScan(m_pScan);
  m_Coder.OpenForRead(io);
}
///

/// ACDifferentialSequentialScan::StartWriteScan
void ACDifferentialSequentialScan::StartWriteScan(class ByteStream *io,class BufferCtrl *ctrl)
{ 
  class ACTemplate *ac,*dc;
  int i;

  for(i = 0;i < m_ucCount;i++) {
    dc = m_pScan->DCConditionerOf(i);
    ac = m_pScan->ACConditionerOf(i);

    if (dc) {
      m_ucSmall[i]    = dc->LowerThresholdOf();
      m_ucLarge[i]    = dc->UpperThresholdOf();
    } else {
      m_ucSmall[i]    = 0;
      m_ucLarge[i]    = 1;
    }

    if (ac) {
      m_ucBlockEnd[i] = ac->BandDiscriminatorOf();
    } else {
      m_ucBlockEnd[i] = 5;
    }

    m_lDiff[i]         = 0;
    m_ulX[i]           = 0;
  }
  m_Context.Init();

  assert(!ctrl->isLineBased());
  m_pBlockCtrl = dynamic_cast<BlockBuffer *>(ctrl);
  m_pBlockCtrl->ResetToStartOfScan(m_pScan);

  m_pScan->WriteMarker(io);
  m_Coder.OpenForWrite(io);
}
///

/// ACDifferentialSequentialScan::StartMeasureScan
// Measure scan statistics.
void ACDifferentialSequentialScan::StartMeasureScan(class BufferCtrl *)
{ 
  //
  // This is not required.
  JPG_THROW(NOT_IMPLEMENTED,"ACDifferentialSequentialScan::StartMeasureScan",
	    "arithmetic coding is always adaptive and does not require "
	    "to measure the statistics");
}
///

/// ACDifferentialSequentialScan::StartMCURow
// Start a MCU scan. Returns true if there are more rows.
bool ACDifferentialSequentialScan::StartMCURow(void)
{
  bool more = m_pBlockCtrl->StartMCUQuantizerRow(m_pScan);

  for(int i = 0;i < m_ucCount;i++) {
    m_ulX[i]   = 0;
  }

  return more;
}
///

/// ACDifferentialSequentialScan::WriteMCU
// Write a single MCU in this scan. Return true if there are more blocks in this row.
bool ACDifferentialSequentialScan::WriteMCU(void)
{ 
  bool more = true;
  int c;

  assert(m_pBlockCtrl);
  
  BeginWriteMCU(m_Coder.ByteStreamOf());

  for(c = 0;c < m_ucCount;c++) {
    class Component *comp    = m_pComponent[c];
    class QuantizedRow *q    = m_pBlockCtrl->CurrentQuantizedRow(comp->IndexOf());
    LONG &prevdiff           = m_lDiff[c];
    UBYTE l                  = m_ucSmall[c];
    UBYTE u                  = m_ucLarge[c];
    UBYTE kx                 = m_ucBlockEnd[c];
    UBYTE mcux               = (m_ucCount > 1)?(comp->MCUWidthOf() ):(1);
    UBYTE mcuy               = (m_ucCount > 1)?(comp->MCUHeightOf()):(1);
    ULONG xmin               = m_ulX[c];
    ULONG xmax               = xmin + mcux;
    ULONG x,y; 
    if (xmax >= q->WidthOf()) {
      more     = false;
    }
    for(y = 0;y < mcuy;y++) {
      for(x = xmin;x < xmax;x++) {
	LONG *block,dummy[64];
	if (q && x < q->WidthOf()) {
	  block  = q->BlockAt(x)->m_Data;
	} else {
	  block  = dummy;
	  memset(dummy ,0,sizeof(dummy) );
	}
	EncodeBlock(block,prevdiff,l,u,kx);
      }
      if (q) q = q->NextOf();
    }
    // Done with this component, advance the block.
    m_ulX[c] = xmax;
  }

  return more;
}
///

/// ACDifferentialSequentialScan::Restart
// Restart the parser at the next restart interval
void ACDifferentialSequentialScan::Restart(void)
{ 
  for(int i = 0;i < m_ucCount;i++) {
    m_lDiff[i]       = 0;
  }
  m_Context.Init();
  m_Coder.OpenForRead(m_Coder.ByteStreamOf());
}
///

/// ACDifferentialSequentialScan::ParseMCU
// Parse a single MCU in this scan. Return true if there are more blocks in this row.
bool ACDifferentialSequentialScan::ParseMCU(void)
{
  bool more = true;
  int c;

  assert(m_pBlockCtrl);

  bool valid = BeginReadMCU(m_Coder.ByteStreamOf());

  for(c = 0;c < m_ucCount;c++) {
    class Component *comp    = m_pComponent[c];
    class QuantizedRow *q    = m_pBlockCtrl->CurrentQuantizedRow(comp->IndexOf());
    LONG &prevdiff           = m_lDiff[c];
    UBYTE l                  = m_ucSmall[c];
    UBYTE u                  = m_ucLarge[c];
    UBYTE kx                 = m_ucBlockEnd[c];
    UBYTE mcux               = (m_ucCount > 1)?(comp->MCUWidthOf() ):(1);
    UBYTE mcuy               = (m_ucCount > 1)?(comp->MCUHeightOf()):(1);
    ULONG xmin               = m_ulX[c];
    ULONG xmax               = xmin + mcux;
    ULONG x,y;
    if (xmax >= q->WidthOf()) {
      more     = false;
    }
    for(y = 0;y < mcuy;y++) {
      for(x = xmin;x < xmax;x++) {
	LONG *block,dummy[64];
	if (q && x < q->WidthOf()) {
	  block  = q->BlockAt(x)->m_Data;
	} else {
	  block  = dummy;
	}
	if (valid) {
	  DecodeBlock(block,prevdiff,l,u,kx);
	} else {
	  for(UBYTE i = m_ucScanStart;i <= m_ucScanStop;i++) {
	    block[i] = 0;
	  }
	}
      }
      if (q) q = q->NextOf();
    }
    // Done with this component, advance the block.
    m_ulX[c] = xmax;
  }

  return more;
}
///

/// ACDifferentialSequentialScan::Classify
// Find the DC context class depending on the previous DC and
// the values of L and U given in the conditioner.
struct ACDifferentialSequentialScan::QMContextSet::DCContextZeroSet &ACDifferentialSequentialScan::Classify(LONG diff,UBYTE l,UBYTE u)
{
  LONG abs = (diff > 0)?(diff):(-diff);
  
  if (abs <= ((1 << l) >> 1)) {
    // the zero cathegory.
    return m_Context.DCZero;
  }
  if (abs <= (1 << u)) {
    if (diff < 0) {
      return m_Context.DCSmallNegative;
    } else {
      return m_Context.DCSmallPositive;
    }
  }
  if (diff < 0) {
    return m_Context.DCLargeNegative;
  } else {
    return m_Context.DCLargePositive;
  }
}
///

/// ACDifferentialSequentialScan::EncodeBlock
// Encode a single huffman block
void ACDifferentialSequentialScan::EncodeBlock(const LONG *block,
					       LONG &prevdiff,
					       UBYTE small,UBYTE large,UBYTE kx)
{
  // DC coding
  if (m_ucScanStart == 0) {
    struct QMContextSet::DCContextZeroSet &cz = Classify(prevdiff,small,large);
    LONG diff;
    // DPCM coding of the DC coefficient.
    diff   = block[0] >> m_ucLowBit; // only correct for two's completement machines
    //
    if (diff) {
      LONG sz;
      //
      // Nonzero, encode a one in context zero.
      m_Coder.Put(cz.S0,true);
      //
      // Sign coding. Encode a zero for positive and a 1 for
      // negative.
      if (diff < 0) {
	m_Coder.Put(cz.SS,true);
	sz = -diff - 1;
      } else {
	m_Coder.Put(cz.SS,false);
	sz = diff - 1;
      }
      //
      // Code the magnitude.
      if (sz >= 1) {
	int  i = 0;
	LONG m = 2;
	m_Coder.Put((diff > 0)?(cz.SP):(cz.SN),true);
	//
	// Magnitude category coding.
	while(sz >= m) {
	  m_Coder.Put(m_Context.DCMagnitude.X[i],true);
	  m <<= 1;
	  i++;
	} 
	// Terminate magnitude cathegory coding.
	m_Coder.Put(m_Context.DCMagnitude.X[i],false);
	//
	// Get the MSB to code.
	m >>= 1;
	// Refinement bits: Depend on the magnitude category.
	while((m >>= 1)) {
	  m_Coder.Put(m_Context.DCMagnitude.M[i],(m & sz)?(true):(false));
	}
      } else {
	m_Coder.Put((diff > 0)?(cz.SP):(cz.SN),false);
      }
    } else {
      // Difference is zero. Encode a zero in context zero.
      m_Coder.Put(cz.S0,false);
    }
    // Keep the difference for the next block. Unclear
    // whether this is really what is supposed to happen here...
    prevdiff = diff;
  }

  if (m_ucScanStop) {
    LONG data;
    int eob,k;
    // AC coding. Part one. Find the end of block.
    // eob is the index of the first zero coefficient from
    // which point on this, and all following coefficients
    // up to coefficient with index 63 are zero.
    eob = m_ucScanStop;
    k   = (m_ucScanStart)?(m_ucScanStart):(1);
    //
    while(eob >= k) {
      data = block[DCT::ScanOrder[eob]];
      if ((data >= 0)?(data >> m_ucLowBit):((-data) >> m_ucLowBit))
	break;
      eob--;
    }
    // The coefficient at eob is now nonzero, but eob+1 is
    // a zero coefficient or beyond the block end.
    eob++; // the first coefficient *not* to code.

    do {
      LONG data,sz;
      //
      if (k == eob) {
	m_Coder.Put(m_Context.ACZero[k-1].SE,true); // Code EOB.
	break;
      }
      // Not EOB.
      m_Coder.Put(m_Context.ACZero[k-1].SE,false);
      //
      // Run coding in S0. Since k is not the eob, at least
      // one non-zero coefficient must follow, so we cannot
      // run over the end of the block.
      do {
	data = block[DCT::ScanOrder[k]];
	data = (data >= 0)?(data >> m_ucLowBit):(-((-data) >> m_ucLowBit));
	if (data == 0) {
	  m_Coder.Put(m_Context.ACZero[k-1].S0,false);
	  k++;
	}
      } while(data == 0);
      m_Coder.Put(m_Context.ACZero[k-1].S0,true);
      //
      // The coefficient at k is now nonzero. First code
      // the sign. This context is the uniform.
      if (data < 0) {
	m_Coder.Put(m_Context.Uniform,true);
	sz = -data - 1;
      } else {
	m_Coder.Put(m_Context.Uniform,false);
	sz =  data - 1;
      }
      //
      // Code the magnitude category. 
      if (sz >= 1) {
	m_Coder.Put(m_Context.ACZero[k-1].SP,true); // SP or SN coding.
	if (sz >= 2) {
	  int  i = 0;
	  LONG m = 4;
	  struct QMContextSet::ACContextMagnitudeSet &acm = (k > kx)?(m_Context.ACMagnitudeHigh):(m_Context.ACMagnitudeLow);
	  //
	  m_Coder.Put(m_Context.ACZero[k-1].SP,true); // X1 coding, identical to SN and SP.
	  // Note that AC_SN,AC_SP and AC_X1 are all the same context
	  // all following decisions are not conditioned on k directly.
	  while(sz >= m) {
	    m_Coder.Put(acm.X[i],true);
	    m <<= 1;
	    i++;
	  }
	  m_Coder.Put(acm.X[i],false);
	  //
	  // Get the MSB to code.
	  m >>= 1;
	  //
	  // Magnitude refinement coding.
	  while((m >>= 1)) {
	    m_Coder.Put(acm.M[i],(m & sz)?true:false);
	  }
	} else {
	  m_Coder.Put(m_Context.ACZero[k-1].SP,false);
	}
      } else {
	m_Coder.Put(m_Context.ACZero[k-1].SP,false);
      }
      //
      // Encode the next coefficient. Note that this bails out early without an
      // S0 encoding if the end is reached.
    } while(++k <= m_ucScanStop);
  }
}
///

/// ACDifferentialSequentialScan::DecodeBlock
// Decode a single huffman block.
void ACDifferentialSequentialScan::DecodeBlock(LONG *block,
					       LONG &prevdiff,
					       UBYTE small,UBYTE large,UBYTE kx)
{
  // DC coding
  if (m_ucScanStart == 0) {
    LONG diff;
    struct QMContextSet::DCContextZeroSet &cz = Classify(prevdiff,small,large);
    // Check whether the difference is nonzero.
    if (m_Coder.Get(cz.S0)) {
      LONG sz;
      bool sign = m_Coder.Get(cz.SS); // sign coding, is true for negative.
      //
      //
      // Positive and negative are encoded in different contexts.
      // Decode the magnitude cathegory.
      if (m_Coder.Get((sign)?(cz.SN):(cz.SP))) {
	int  i = 0;
	LONG m = 2;
	
	while(m_Coder.Get(m_Context.DCMagnitude.X[i])) {
	  m <<= 1;
	  i++;
	  if (m == 0) 
	    JPG_THROW(MALFORMED_STREAM,"ACDifferentialSequentialScan::DecodeBlock",
		      "QMDecoder is out of sync");
	}
	//
	// Get the MSB to decode.
	m >>= 1;
	sz  = m;
	//
	// Refinement coding of remaining bits.
	while((m >>= 1)) {
	  if (m_Coder.Get(m_Context.DCMagnitude.M[i])) {
	    sz |= m;
	  }
	}
      } else {
	sz = 0;
      }
      //
      // Done, finally, include the sign and the offset.
      if (sign) {
	diff = -sz - 1;
      } else {
	diff = sz + 1;
      }
    } else {
      // Difference is zero.
      diff = 0;
    }

    prevdiff = diff;
    block[0] = diff << m_ucLowBit; // point transformation
  }

  if (m_ucScanStop) {
    // AC coding. No block skipping used here.
    int k = (m_ucScanStart)?(m_ucScanStart):(1);
    //
    // EOB decoding.
    while(k <= m_ucScanStop && !m_Coder.Get(m_Context.ACZero[k-1].SE)) {
      LONG sz;
      bool sign;
      //
      // Not yet EOB. Run coding in S0: Skip over zeros.
      while(!m_Coder.Get(m_Context.ACZero[k-1].S0)) {
	k++;
	if (k > m_ucScanStop)
	  JPG_THROW(MALFORMED_STREAM,"ACDifferentialSequentialScan::DecodeBlock",
		    "QMDecoder is out of sync");
      }
      //
      // Now decode the sign of the coefficient.
      // This happens in the uniform context.
      sign = m_Coder.Get(m_Context.Uniform);
      //
      // Decode the magnitude.
      if (m_Coder.Get(m_Context.ACZero[k-1].SP)) {
	// X1 coding, identical to SN and SP.
	if (m_Coder.Get(m_Context.ACZero[k-1].SP)) {
	  int  i = 0;
	  LONG m = 4;
	  struct QMContextSet::ACContextMagnitudeSet &acm = (k > kx)?(m_Context.ACMagnitudeHigh):(m_Context.ACMagnitudeLow);
	  
	  while(m_Coder.Get(acm.X[i])) {
	    m <<= 1;
	    i++;
	    if (m == 0)
	      JPG_THROW(MALFORMED_STREAM,"ACDifferentialSequentialScan::DecodeBlock",
			"QMDecoder is out of sync");
	  }
	  //
	  // Get the MSB to decode
	  m >>= 1;
	  sz  = m;
	  //
	  // Proceed to refinement.
	  while((m >>= 1)) {
	    if (m_Coder.Get(acm.M[i])) {
	      sz |= m;
	    }
	  }
	} else {
	  sz = 1;
	}
      } else {
	sz = 0;
      }
      //
      // Done. Finally, include sign and offset.
      sz++;
      if (sign) 
	sz = -sz;
      block[DCT::ScanOrder[k]] = sz << m_ucLowBit;
      //
      // Proceed to the next block.
      k++;
    }
  }
}
///

/// ACDifferentialSequentialScan::WriteFrameType
// Write the marker that indicates the frame type fitting to this scan.
void ACDifferentialSequentialScan::WriteFrameType(class ByteStream *io)
{
  if (m_ucScanStart > 0 || m_ucScanStop < 63 || m_ucLowBit) {
    // is progressive.
    io->PutWord(0xffce);
  } else {
    io->PutWord(0xffcd); // AC differential sequential
  }
}
///

/// ACDifferentialSequentialScan::Flush
// Flush the remaining bits out to the stream on writing.
void ACDifferentialSequentialScan::Flush(void)
{
  m_Coder.Flush();
  m_Context.Init();

  for(int i = 0;i < m_ucCount;i++) {
    m_lDiff[i]  = 0;
  }

  m_Coder.OpenForWrite(m_Coder.ByteStreamOf());
}
///
