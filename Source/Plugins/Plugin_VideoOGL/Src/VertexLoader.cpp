// Copyright (C) 2003-2008 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "Globals.h"

#include <fstream>
#include <assert.h>

#include "Common.h"
#include "Config.h"
#include "ImageWrite.h"
#include "Profiler.h"
#include "StringUtil.h"

#include "Render.h"
#include "VertexShader.h"
#include "VertexManager.h"
#include "VertexLoaderManager.h"
#include "VertexLoader.h"
#include "BPStructs.h"
#include "DataReader.h"

#include "VertexShaderManager.h"
#include "PixelShaderManager.h"
#include "TextureMngr.h"

#include <fstream>

NativeVertexFormat *g_nativeVertexFmt;

//these don't need to be saved
static float posScale;
static int colElements[2];
static float tcScaleU[8];
static float tcScaleV[8];
static int tcIndex;
static int colIndex;
#ifndef _WIN32
    #undef inline
    #define inline
#endif


// ==============================================================================
// Direct
// ==============================================================================
static u8 s_curposmtx;
static u8 s_curtexmtx[8];
static int s_texmtxwrite = 0;
static int s_texmtxread = 0;

void LOADERDECL PosMtx_ReadDirect_UByte(const void *_p)
{
    s_curposmtx = DataReadU8()&0x3f;
    PRIM_LOG("posmtx: %d, ", s_curposmtx);
}

void LOADERDECL PosMtx_Write(const void *_p)
{
    *VertexManager::s_pCurBufferPointer++ = s_curposmtx;
    //*VertexManager::s_pCurBufferPointer++ = 0;
    //*VertexManager::s_pCurBufferPointer++ = 0;
    //*VertexManager::s_pCurBufferPointer++ = 0;
}

void LOADERDECL TexMtx_ReadDirect_UByte(const void *_p)
{
    s_curtexmtx[s_texmtxread] = DataReadU8()&0x3f;
    PRIM_LOG("texmtx%d: %d, ", s_texmtxread, s_curtexmtx[s_texmtxread]);
    s_texmtxread++;
}

void LOADERDECL TexMtx_Write_Float(const void *_p)
{
    *(float*)VertexManager::s_pCurBufferPointer = (float)s_curtexmtx[s_texmtxwrite++];
    VertexManager::s_pCurBufferPointer += 4;
}

void LOADERDECL TexMtx_Write_Float2(const void *_p)
{
    ((float*)VertexManager::s_pCurBufferPointer)[0] = 0;
    ((float*)VertexManager::s_pCurBufferPointer)[1] = (float)s_curtexmtx[s_texmtxwrite++];
    VertexManager::s_pCurBufferPointer += 8;
}

void LOADERDECL TexMtx_Write_Short3(const void *_p)
{
    ((s16*)VertexManager::s_pCurBufferPointer)[0] = 0;
    ((s16*)VertexManager::s_pCurBufferPointer)[1] = 0;
    ((s16*)VertexManager::s_pCurBufferPointer)[2] = s_curtexmtx[s_texmtxwrite++];
    VertexManager::s_pCurBufferPointer += 6;
}

#include "VertexLoader_Position.h"
#include "VertexLoader_Normal.h"
#include "VertexLoader_Color.h"
#include "VertexLoader_TextCoord.h"

VertexLoader::VertexLoader() 
{
    m_VertexSize = 0;
    m_AttrDirty = AD_DIRTY;
	m_numPipelineStages = 0;
    VertexLoader_Normal::Init();
}

VertexLoader::~VertexLoader() 
{
}

int VertexLoader::ComputeVertexSize()
{
    if (m_AttrDirty == AD_CLEAN) {
		// Compare the 33 desc bits. 
        if (m_VtxDesc.Hex0 == g_VtxDesc.Hex0 &&
		    (m_VtxDesc.Hex1 & 1) == (g_VtxDesc.Hex1 & 1))
            return m_VertexSize;

        m_VtxDesc.Hex = g_VtxDesc.Hex;
    }
    else {
        // Attributes are dirty so we have to recompute everything anyway.
        m_VtxDesc.Hex = g_VtxDesc.Hex;
    }

    m_AttrDirty = AD_DIRTY;
    m_VertexSize = 0;
    // Position Matrix Index
    if (m_VtxDesc.PosMatIdx)
        m_VertexSize += 1;

    // Texture matrix indices
    if (m_VtxDesc.Tex0MatIdx) m_VertexSize += 1;
    if (m_VtxDesc.Tex1MatIdx) m_VertexSize += 1;
    if (m_VtxDesc.Tex2MatIdx) m_VertexSize += 1;
    if (m_VtxDesc.Tex3MatIdx) m_VertexSize += 1;
    if (m_VtxDesc.Tex4MatIdx) m_VertexSize += 1;
    if (m_VtxDesc.Tex5MatIdx) m_VertexSize += 1;
    if (m_VtxDesc.Tex6MatIdx) m_VertexSize += 1;
    if (m_VtxDesc.Tex7MatIdx) m_VertexSize += 1;
    
    switch (m_VtxDesc.Position) {
    case NOT_PRESENT:	{_assert_("Vertex descriptor without position!");} break;
    case DIRECT:
        {
            switch (m_VtxAttr.PosFormat) {
            case FORMAT_UBYTE:
            case FORMAT_BYTE: m_VertexSize += m_VtxAttr.PosElements?3:2; break;
            case FORMAT_USHORT:
            case FORMAT_SHORT: m_VertexSize += m_VtxAttr.PosElements?6:4; break;
            case FORMAT_FLOAT: m_VertexSize += m_VtxAttr.PosElements?12:8; break;
            default: _assert_(0); break;
            }
        }
        break;
    case INDEX8:		
        m_VertexSize += 1;
        break;
    case INDEX16:
        m_VertexSize += 2;
        break;
    }

	VertexLoader_Normal::index3 = m_VtxAttr.NormalIndex3 ? true : false;
    if (m_VtxDesc.Normal != NOT_PRESENT)
        m_VertexSize += VertexLoader_Normal::GetSize(m_VtxDesc.Normal, m_VtxAttr.NormalFormat, m_VtxAttr.NormalElements);
    
    // Colors
    int col[2] = {m_VtxDesc.Color0, m_VtxDesc.Color1};
    for (int i = 0; i < 2; i++) {
        switch (col[i])
        {
        case NOT_PRESENT: 
            break;
        case DIRECT:
            switch (m_VtxAttr.color[i].Comp)
            {
            case FORMAT_16B_565:	m_VertexSize += 2; break;
            case FORMAT_24B_888:	m_VertexSize += 3; break;
            case FORMAT_32B_888x:	m_VertexSize += 4; break;
            case FORMAT_16B_4444:	m_VertexSize += 2; break;
            case FORMAT_24B_6666:	m_VertexSize += 3; break;
            case FORMAT_32B_8888:	m_VertexSize += 4; break;
            default: _assert_(0); break;
            }									    
            break;
        case INDEX8:	
            m_VertexSize += 1;
            break;
        case INDEX16:
            m_VertexSize += 2;
            break;
        }   
    }

    // TextureCoord
    int tc[8] = {
        m_VtxDesc.Tex0Coord, m_VtxDesc.Tex1Coord, m_VtxDesc.Tex2Coord, m_VtxDesc.Tex3Coord,
        m_VtxDesc.Tex4Coord, m_VtxDesc.Tex5Coord, m_VtxDesc.Tex6Coord, m_VtxDesc.Tex7Coord,
    };
    
    for (int i = 0; i < 8; i++) {
        switch (tc[i]) {
        case NOT_PRESENT: 
            break;
        case DIRECT: 
            {
                switch (m_VtxAttr.texCoord[i].Format)
                {
                case FORMAT_UBYTE:
                case FORMAT_BYTE: m_VertexSize += m_VtxAttr.texCoord[i].Elements?2:1; break;
                case FORMAT_USHORT:
                case FORMAT_SHORT: m_VertexSize += m_VtxAttr.texCoord[i].Elements?4:2; break;
                case FORMAT_FLOAT: m_VertexSize += m_VtxAttr.texCoord[i].Elements?8:4; break;
                default: _assert_(0); break;
                }
            }
            break;
        case INDEX8:	
            m_VertexSize += 1;
            break;
        case INDEX16:
            m_VertexSize += 2;
            break;
        }
    }

    return m_VertexSize;
}


void VertexLoader::CompileVertexTranslator()
{
    if (m_AttrDirty == AD_CLEAN)
    {
		// Check if local cached desc (in this VL) matches global desc
        if (m_VtxDesc.Hex0 == g_VtxDesc.Hex0 &&
		    (m_VtxDesc.Hex1 & 1) == (g_VtxDesc.Hex1 & 1))
		{
            return;  // same
		}
    }
    else
	{
        m_AttrDirty = AD_CLEAN;
	}
     
    m_VtxDesc.Hex = g_VtxDesc.Hex;

    // Reset pipeline
    m_numPipelineStages = 0;

	// It's a bit ugly that we poke inside m_NativeFmt in this function. Planning to fix this.
    m_NativeFmt.m_VBStridePad = 0;
    m_NativeFmt.m_VBVertexStride = 0;
    m_NativeFmt.m_components = 0;

    // m_VBVertexStride for texmtx and posmtx is computed later when writing.
    
    // Position Matrix Index
    if (m_VtxDesc.PosMatIdx) {
        m_PipelineStages[m_numPipelineStages++] = PosMtx_ReadDirect_UByte;
        m_NativeFmt.m_components |= VB_HAS_POSMTXIDX;
    }

    if (m_VtxDesc.Tex0MatIdx) {m_NativeFmt.m_components |= VB_HAS_TEXMTXIDX0; WriteCall(TexMtx_ReadDirect_UByte); }
	if (m_VtxDesc.Tex1MatIdx) {m_NativeFmt.m_components |= VB_HAS_TEXMTXIDX1; WriteCall(TexMtx_ReadDirect_UByte); }
	if (m_VtxDesc.Tex2MatIdx) {m_NativeFmt.m_components |= VB_HAS_TEXMTXIDX2; WriteCall(TexMtx_ReadDirect_UByte); }
	if (m_VtxDesc.Tex3MatIdx) {m_NativeFmt.m_components |= VB_HAS_TEXMTXIDX3; WriteCall(TexMtx_ReadDirect_UByte); }
	if (m_VtxDesc.Tex4MatIdx) {m_NativeFmt.m_components |= VB_HAS_TEXMTXIDX4; WriteCall(TexMtx_ReadDirect_UByte); }
	if (m_VtxDesc.Tex5MatIdx) {m_NativeFmt.m_components |= VB_HAS_TEXMTXIDX5; WriteCall(TexMtx_ReadDirect_UByte); }
	if (m_VtxDesc.Tex6MatIdx) {m_NativeFmt.m_components |= VB_HAS_TEXMTXIDX6; WriteCall(TexMtx_ReadDirect_UByte); }
	if (m_VtxDesc.Tex7MatIdx) {m_NativeFmt.m_components |= VB_HAS_TEXMTXIDX7; WriteCall(TexMtx_ReadDirect_UByte); }

    // Position
    if (m_VtxDesc.Position != NOT_PRESENT)
        m_NativeFmt.m_VBVertexStride += 12;

    switch (m_VtxDesc.Position) {
    case NOT_PRESENT:	{_assert_msg_(0, "Vertex descriptor without position!", "WTF?");} break;
    case DIRECT:
        {
            switch (m_VtxAttr.PosFormat) {
            case FORMAT_UBYTE:	WriteCall(Pos_ReadDirect_UByte);  break;
            case FORMAT_BYTE:	WriteCall(Pos_ReadDirect_Byte);   break;
            case FORMAT_USHORT:	WriteCall(Pos_ReadDirect_UShort); break;
            case FORMAT_SHORT:	WriteCall(Pos_ReadDirect_Short);  break;
            case FORMAT_FLOAT:	WriteCall(Pos_ReadDirect_Float);  break;
            default: _assert_(0); break;
            }
        }
        break;
    case INDEX8:		
        switch (m_VtxAttr.PosFormat) {
        case FORMAT_UBYTE:	WriteCall(Pos_ReadIndex8_UByte);  break; //WTF?
        case FORMAT_BYTE:	WriteCall(Pos_ReadIndex8_Byte);   break;
        case FORMAT_USHORT:	WriteCall(Pos_ReadIndex8_UShort); break;
        case FORMAT_SHORT:	WriteCall(Pos_ReadIndex8_Short);  break;
        case FORMAT_FLOAT:	WriteCall(Pos_ReadIndex8_Float);  break;
        default: _assert_(0); break;
        }
        break;
    case INDEX16:
        switch (m_VtxAttr.PosFormat) {
        case FORMAT_UBYTE:	WriteCall(Pos_ReadIndex16_UByte);  break;
        case FORMAT_BYTE:	WriteCall(Pos_ReadIndex16_Byte);   break;
        case FORMAT_USHORT:	WriteCall(Pos_ReadIndex16_UShort); break;
        case FORMAT_SHORT:	WriteCall(Pos_ReadIndex16_Short);  break;
        case FORMAT_FLOAT:	WriteCall(Pos_ReadIndex16_Float);  break;
        default: _assert_(0); break;
        }
        break;
    }

    // Normals
    if (m_VtxDesc.Normal != NOT_PRESENT) {
        VertexLoader_Normal::index3 = m_VtxAttr.NormalIndex3 ? true : false;
        TPipelineFunction pFunc = VertexLoader_Normal::GetFunction(m_VtxDesc.Normal, m_VtxAttr.NormalFormat, m_VtxAttr.NormalElements);
        if (pFunc == 0)
        {
            char temp[256];
            sprintf(temp,"%i %i %i", m_VtxDesc.Normal, m_VtxAttr.NormalFormat, m_VtxAttr.NormalElements);
            g_VideoInitialize.pSysMessage("VertexLoader_Normal::GetFunction returned zero!");
        }
        WriteCall(pFunc);

        int sizePro = 0;
        switch (m_VtxAttr.NormalFormat)
        {
        case FORMAT_UBYTE:	sizePro=1; break;
        case FORMAT_BYTE:	sizePro=1; break;
        case FORMAT_USHORT:	sizePro=2; break;
        case FORMAT_SHORT:	sizePro=2; break;
        case FORMAT_FLOAT:	sizePro=4; break;
        default: _assert_(0); break;
        }
        m_NativeFmt.m_VBVertexStride += sizePro * 3 * (m_VtxAttr.NormalElements?3:1);

        int numNormals = (m_VtxAttr.NormalElements == 1) ? NRM_THREE : NRM_ONE;
        m_NativeFmt.m_components |= VB_HAS_NRM0;
        if (numNormals == NRM_THREE)
            m_NativeFmt.m_components |= VB_HAS_NRM1 | VB_HAS_NRM2;
    }
    
    // Colors
    int col[2] = {m_VtxDesc.Color0, m_VtxDesc.Color1};
    for (int i = 0; i < 2; i++) {
        SetupColor(i, col[i], m_VtxAttr.color[i].Comp, m_VtxAttr.color[i].Elements);

        if (col[i] != NOT_PRESENT)
            m_NativeFmt.m_VBVertexStride += 4;
    }

    // TextureCoord
    int tc[8] = {
        m_VtxDesc.Tex0Coord, m_VtxDesc.Tex1Coord, m_VtxDesc.Tex2Coord, m_VtxDesc.Tex3Coord,
        m_VtxDesc.Tex4Coord, m_VtxDesc.Tex5Coord, m_VtxDesc.Tex6Coord, m_VtxDesc.Tex7Coord,
    };
    
    // Texture matrix indices (remove if corresponding texture coordinate isn't enabled)
    for (int i = 0; i < 8; i++) {
        SetupTexCoord(i, tc[i], m_VtxAttr.texCoord[i].Format, m_VtxAttr.texCoord[i].Elements, m_VtxAttr.texCoord[i].Frac);
        if (m_NativeFmt.m_components & (VB_HAS_TEXMTXIDX0 << i)) {
            if (tc[i] != NOT_PRESENT) {
                // if texmtx is included, texcoord will always be 3 floats, z will be the texmtx index
                WriteCall(m_VtxAttr.texCoord[i].Elements ? TexMtx_Write_Float : TexMtx_Write_Float2);
                m_NativeFmt.m_VBVertexStride += 12;
            }
            else {
                WriteCall(TexMtx_Write_Short3);
                m_NativeFmt.m_VBVertexStride += 6; // still include the texture coordinate, but this time as 6 bytes
                m_NativeFmt.m_components |= VB_HAS_UV0 << i; // have to include since using now
            }
        }
        else {
            if (tc[i] != NOT_PRESENT)
                m_NativeFmt.m_VBVertexStride += 4 * (m_VtxAttr.texCoord[i].Elements ? 2 : 1);
        }

        if (tc[i] == NOT_PRESENT) {
            // if there's more tex coords later, have to write a dummy call 
            int j = i + 1;
            for (; j < 8; ++j) {
                if (tc[j] != NOT_PRESENT) {
                    WriteCall(TexCoord_Read_Dummy); // important to get indices right!
                    break;
                }
            }
            if (j == 8 && !((m_NativeFmt.m_components&VB_HAS_TEXMTXIDXALL) & (VB_HAS_TEXMTXIDXALL<<(i+1)))) // no more tex coords and tex matrices, so exit loop
                break;
        }
    }

    if (m_VtxDesc.PosMatIdx) {
        WriteCall(PosMtx_Write);
        m_NativeFmt.m_VBVertexStride += 1;
    }

	m_NativeFmt.Initialize(m_VtxDesc, m_VtxAttr);
}

void VertexLoader::SetupColor(int num, int mode, int format, int elements)
{
    // if COL0 not present, then embed COL1 into COL0
    if (num == 1 && !(m_NativeFmt.m_components & VB_HAS_COL0))
		num = 0;

    m_NativeFmt.m_components |= VB_HAS_COL0 << num;
    switch (mode)
    {
    case NOT_PRESENT: 
        m_NativeFmt.m_components &= ~(VB_HAS_COL0 << num);
        break;
    case DIRECT:
        switch (format)
        {
        case FORMAT_16B_565:	WriteCall(Color_ReadDirect_16b_565); break;
        case FORMAT_24B_888:	WriteCall(Color_ReadDirect_24b_888); break;
        case FORMAT_32B_888x:	WriteCall(Color_ReadDirect_32b_888x); break;
        case FORMAT_16B_4444:	WriteCall(Color_ReadDirect_16b_4444); break;
        case FORMAT_24B_6666:	WriteCall(Color_ReadDirect_24b_6666); break;
        case FORMAT_32B_8888:	WriteCall(Color_ReadDirect_32b_8888); break;
        default: _assert_(0); break;
        }
        break;
    case INDEX8:	
        switch (format)
        {
        case FORMAT_16B_565:	WriteCall(Color_ReadIndex8_16b_565); break;
        case FORMAT_24B_888:	WriteCall(Color_ReadIndex8_24b_888); break;
        case FORMAT_32B_888x:	WriteCall(Color_ReadIndex8_32b_888x); break;
        case FORMAT_16B_4444:	WriteCall(Color_ReadIndex8_16b_4444); break;
        case FORMAT_24B_6666:	WriteCall(Color_ReadIndex8_24b_6666); break;
        case FORMAT_32B_8888:	WriteCall(Color_ReadIndex8_32b_8888); break;
        default: _assert_(0); break;
        }
        break;
    case INDEX16:
        switch (format)
        {
        case FORMAT_16B_565:	WriteCall(Color_ReadIndex16_16b_565); break;
        case FORMAT_24B_888:	WriteCall(Color_ReadIndex16_24b_888); break;
        case FORMAT_32B_888x:	WriteCall(Color_ReadIndex16_32b_888x); break;
        case FORMAT_16B_4444:	WriteCall(Color_ReadIndex16_16b_4444); break;
        case FORMAT_24B_6666:	WriteCall(Color_ReadIndex16_24b_6666); break;
        case FORMAT_32B_8888:	WriteCall(Color_ReadIndex16_32b_8888); break;
        default: _assert_(0); break;
        }
        break;
    }
}

void VertexLoader::SetupTexCoord(int num, int mode, int format, int elements, int _iFrac)
{
    m_NativeFmt.m_components |= VB_HAS_UV0 << num;
    
    switch (mode)
    {
    case NOT_PRESENT: 
        m_NativeFmt.m_components &= ~(VB_HAS_UV0 << num);
        break;
    case DIRECT:
        switch (format)
        {
        case FORMAT_UBYTE:	WriteCall(elements?TexCoord_ReadDirect_UByte2:TexCoord_ReadDirect_UByte1);  break;
        case FORMAT_BYTE:	WriteCall(elements?TexCoord_ReadDirect_Byte2:TexCoord_ReadDirect_Byte1);   break;
        case FORMAT_USHORT:	WriteCall(elements?TexCoord_ReadDirect_UShort2:TexCoord_ReadDirect_UShort1); break;
        case FORMAT_SHORT:	WriteCall(elements?TexCoord_ReadDirect_Short2:TexCoord_ReadDirect_Short1);  break;
        case FORMAT_FLOAT:	WriteCall(elements?TexCoord_ReadDirect_Float2:TexCoord_ReadDirect_Float1);  break;
        default: _assert_(0); break;
        }
        break;
    case INDEX8:	
        switch (format)
        {
        case FORMAT_UBYTE:	WriteCall(elements?TexCoord_ReadIndex8_UByte2:TexCoord_ReadIndex8_UByte1);  break;
        case FORMAT_BYTE:	WriteCall(elements?TexCoord_ReadIndex8_Byte2:TexCoord_ReadIndex8_Byte1);   break;
        case FORMAT_USHORT:	WriteCall(elements?TexCoord_ReadIndex8_UShort2:TexCoord_ReadIndex8_UShort1); break;
        case FORMAT_SHORT:	WriteCall(elements?TexCoord_ReadIndex8_Short2:TexCoord_ReadIndex8_Short1);  break;
        case FORMAT_FLOAT:	WriteCall(elements?TexCoord_ReadIndex8_Float2:TexCoord_ReadIndex8_Float1);  break;
        default: _assert_(0); break;
        }
        break;
    case INDEX16:
        switch (format)
        {
        case FORMAT_UBYTE:	WriteCall(elements?TexCoord_ReadIndex16_UByte2:TexCoord_ReadIndex16_UByte1);  break;
        case FORMAT_BYTE:	WriteCall(elements?TexCoord_ReadIndex16_Byte2:TexCoord_ReadIndex16_Byte1);   break;
        case FORMAT_USHORT:	WriteCall(elements?TexCoord_ReadIndex16_UShort2:TexCoord_ReadIndex16_UShort1); break;
        case FORMAT_SHORT:	WriteCall(elements?TexCoord_ReadIndex16_Short2:TexCoord_ReadIndex16_Short1);  break;
        case FORMAT_FLOAT:	WriteCall(elements?TexCoord_ReadIndex16_Float2:TexCoord_ReadIndex16_Float1);  break;
        default: _assert_(0);
        }
        break;
    }
}

void VertexLoader::WriteCall(TPipelineFunction func)
{
	m_PipelineStages[m_numPipelineStages++] = func;
}

void VertexLoader::RunVertices(int primitive, int count)
{
    DVSTARTPROFILE();

	// Flush if our vertex format is different from the currently set.
	// TODO - this check should be moved.
    if (g_nativeVertexFmt != NULL && g_nativeVertexFmt != &m_NativeFmt)
	{
        VertexManager::Flush();
		// Also move the Set() here?
	}
	g_nativeVertexFmt = &m_NativeFmt;

	// This has dirty handling - won't actually recompute unless necessary.
	ComputeVertexSize();

    if (bpmem.genMode.cullmode == 3 && primitive < 5)
	{
        // if cull mode is none, ignore triangles and quads
		DataSkip(count * m_VertexSize);
        return;
    }

	// This has dirty handling - won't actually recompute unless necessary.
    CompileVertexTranslator();

	VertexManager::EnableComponents(m_NativeFmt.m_components);

    // Load position and texcoord scale factors.
	// Hm, this could be done when the VtxAttr is set, instead.
	posScale = shiftLookup[m_VtxAttr.PosFrac];
    if (m_NativeFmt.m_components & VB_HAS_UVALL) {
        for (int i = 0; i < 8; i++) {
            tcScaleU[i] = shiftLookup[m_VtxAttr.texCoord[i].Frac];
            tcScaleV[i] = shiftLookup[m_VtxAttr.texCoord[i].Frac];
        }
    }
    for (int i = 0; i < 2; i++)
        colElements[i] = m_VtxAttr.color[i].Elements;

    // if strips or fans, make sure all vertices can fit in buffer, otherwise flush
    int granularity = 1;
    switch (primitive) {
        case 3: // strip
        case 4: // fan
            if (VertexManager::GetRemainingSize() < 3 * m_NativeFmt.m_VBVertexStride )
                VertexManager::Flush();
            break;
        case 6: // line strip
            if (VertexManager::GetRemainingSize() < 2 * m_NativeFmt.m_VBVertexStride )
                VertexManager::Flush();
            break;
        case 0: // quads
            granularity = 4;
            break;
        case 2: // tris
            granularity = 3;
            break;
        case 5: // lines
            granularity = 2;
            break;
    }

    int startv = 0, extraverts = 0;
    for (int v = 0; v < count; v++)
	{
        if ((v % granularity) == 0)
		{
            if (VertexManager::GetRemainingSize() < granularity*m_NativeFmt.m_VBVertexStride) {
				// This buffer full - break current primitive and flush, to switch to the next buffer.
                u8* plastptr = VertexManager::s_pCurBufferPointer;
                if (v - startv > 0)
                    VertexManager::AddVertices(primitive, v - startv + extraverts);
                VertexManager::Flush();
				// Why does this need to be so complicated?
                switch (primitive) {
                    case 3: // triangle strip, copy last two vertices
                        // a little trick since we have to keep track of signs
                        if (v & 1) {
                            memcpy_gc(VertexManager::s_pCurBufferPointer, plastptr-2*m_NativeFmt.m_VBVertexStride, m_NativeFmt.m_VBVertexStride);
                            memcpy_gc(VertexManager::s_pCurBufferPointer+m_NativeFmt.m_VBVertexStride, plastptr-m_NativeFmt.m_VBVertexStride*2, 2*m_NativeFmt.m_VBVertexStride);
                            VertexManager::s_pCurBufferPointer += m_NativeFmt.m_VBVertexStride*3;
                            extraverts = 3;
                        }
                        else {
                            memcpy_gc(VertexManager::s_pCurBufferPointer, plastptr-m_NativeFmt.m_VBVertexStride*2, m_NativeFmt.m_VBVertexStride*2);
                            VertexManager::s_pCurBufferPointer += m_NativeFmt.m_VBVertexStride*2;
                            extraverts = 2;
                        }
                        break;
                    case 4: // tri fan, copy first and last vert
                        memcpy_gc(VertexManager::s_pCurBufferPointer, plastptr-m_NativeFmt.m_VBVertexStride*(v-startv+extraverts), m_NativeFmt.m_VBVertexStride);
                        VertexManager::s_pCurBufferPointer += m_NativeFmt.m_VBVertexStride;
                        memcpy_gc(VertexManager::s_pCurBufferPointer, plastptr-m_NativeFmt.m_VBVertexStride, m_NativeFmt.m_VBVertexStride);
                        VertexManager::s_pCurBufferPointer += m_NativeFmt.m_VBVertexStride;
                        extraverts = 2;
                        break;
                    case 6: // line strip
                        memcpy_gc(VertexManager::s_pCurBufferPointer, plastptr-m_NativeFmt.m_VBVertexStride, m_NativeFmt.m_VBVertexStride);
                        VertexManager::s_pCurBufferPointer += m_NativeFmt.m_VBVertexStride;
                        extraverts = 1;
                        break;
                    default:
                        extraverts = 0;
						break;
                }
                startv = v;
            }
        }
        tcIndex = 0;
        colIndex = 0;
        s_texmtxwrite = s_texmtxread = 0;

		RunPipelineOnce();

        VertexManager::s_pCurBufferPointer += m_NativeFmt.m_VBStridePad;
        PRIM_LOG("\n");
    }

    if (startv < count)
        VertexManager::AddVertices(primitive, count - startv + extraverts);
}

void VertexLoader::RunPipelineOnce() const
{
	for (int i = 0; i < m_numPipelineStages; i++)
		m_PipelineStages[i](&m_VtxAttr);
}

void VertexLoader::SetVAT_group0(u32 _group0) 
{
	// ignore frac bits - we don't need to recompute if all that's changed was the frac bits.
    if ((m_group0.Hex & ~VAT_0_FRACBITS) != (_group0 & ~VAT_0_FRACBITS)) {
        m_AttrDirty = AD_VAT_DIRTY;
    }
    m_group0.Hex = _group0;

    m_VtxAttr.PosElements			= m_group0.PosElements;
    m_VtxAttr.PosFormat				= m_group0.PosFormat;
    m_VtxAttr.PosFrac				= m_group0.PosFrac;
    m_VtxAttr.NormalElements		= m_group0.NormalElements;
    m_VtxAttr.NormalFormat			= m_group0.NormalFormat;
    m_VtxAttr.color[0].Elements		= m_group0.Color0Elements;
    m_VtxAttr.color[0].Comp			= m_group0.Color0Comp;
    m_VtxAttr.color[1].Elements		= m_group0.Color1Elements;
    m_VtxAttr.color[1].Comp			= m_group0.Color1Comp;
    m_VtxAttr.texCoord[0].Elements	= m_group0.Tex0CoordElements;
    m_VtxAttr.texCoord[0].Format	= m_group0.Tex0CoordFormat;
    m_VtxAttr.texCoord[0].Frac		= m_group0.Tex0Frac;
    m_VtxAttr.ByteDequant			= m_group0.ByteDequant;
    m_VtxAttr.NormalIndex3			= m_group0.NormalIndex3;
};

void VertexLoader::SetVAT_group1(u32 _group1) 
{
    if ((m_group1.Hex & ~VAT_1_FRACBITS) != (_group1 & ~VAT_1_FRACBITS)) {
        m_AttrDirty = AD_VAT_DIRTY;
    }
    m_group1.Hex = _group1;

    m_VtxAttr.texCoord[1].Elements	= m_group1.Tex1CoordElements;
    m_VtxAttr.texCoord[1].Format	= m_group1.Tex1CoordFormat;
    m_VtxAttr.texCoord[1].Frac		= m_group1.Tex1Frac;
    m_VtxAttr.texCoord[2].Elements	= m_group1.Tex2CoordElements;
    m_VtxAttr.texCoord[2].Format	= m_group1.Tex2CoordFormat;
    m_VtxAttr.texCoord[2].Frac		= m_group1.Tex2Frac;
    m_VtxAttr.texCoord[3].Elements	= m_group1.Tex3CoordElements;
    m_VtxAttr.texCoord[3].Format	= m_group1.Tex3CoordFormat;
    m_VtxAttr.texCoord[3].Frac      = m_group1.Tex3Frac;
    m_VtxAttr.texCoord[4].Elements	= m_group1.Tex4CoordElements;
    m_VtxAttr.texCoord[4].Format	= m_group1.Tex4CoordFormat;
};									  
                                      
void VertexLoader::SetVAT_group2(u32 _group2)		  
{
    if ((m_group2.Hex & ~VAT_2_FRACBITS) != (_group2 & ~VAT_2_FRACBITS)) {
        m_AttrDirty = AD_VAT_DIRTY;
    }
    m_group2.Hex = _group2;

    m_VtxAttr.texCoord[4].Frac		= m_group2.Tex4Frac;
    m_VtxAttr.texCoord[5].Elements	= m_group2.Tex5CoordElements;
    m_VtxAttr.texCoord[5].Format	= m_group2.Tex5CoordFormat;
    m_VtxAttr.texCoord[5].Frac		= m_group2.Tex5Frac;
    m_VtxAttr.texCoord[6].Elements	= m_group2.Tex6CoordElements;
    m_VtxAttr.texCoord[6].Format	= m_group2.Tex6CoordFormat;
    m_VtxAttr.texCoord[6].Frac		= m_group2.Tex6Frac;
    m_VtxAttr.texCoord[7].Elements	= m_group2.Tex7CoordElements;
    m_VtxAttr.texCoord[7].Format	= m_group2.Tex7CoordFormat;
    m_VtxAttr.texCoord[7].Frac		= m_group2.Tex7Frac;
};
