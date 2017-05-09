/*
 * Adplug - Replayer for many OPL2/OPL3 audio file formats.
 * Copyright (C) 1999 - 2008 Simon Peter <dn.tlp@gmx.net>, et al.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * herad.cpp - Herbulot AdLib Player by Stas'M <binarymaster@mail.ru>
 *
 * REFERENCES:
 * http://www.vgmpf.com/Wiki/index.php/HERAD
 *
 * TODO:
 * - Implement looping
 * - Fix transpose issue
 * - Fix strange AGD sound
 */

#include <cstring>
#include <stdio.h>

#include "herad.h"

#ifdef DEBUG
#include "debug.h"
#endif

const uint8_t CheradPlayer::slot_offset[9] = {
	0, 1, 2, 8, 9, 10, 16, 17, 18
};
const uint16_t CheradPlayer::FNum[12] = {
	343, 364, 385, 408, 433, 459, 486, 515, 546, 579, 614, 650
};
const uint16_t CheradPlayer::FNum_coarse[12 * 5] = {
	343, 348, 353, 358, 363,
	364, 369, 374, 379, 384,
	385, 390, 395, 400, 405,
	408, 413, 418, 423, 428,
	433, 438, 443, 448, 453,
	459, 464, 469, 474, 479,
	486, 492, 498, 504, 510,
	515, 521, 527, 533, 539,
	546, 552, 558, 564, 570,
	579, 585, 591, 597, 603,
	614, 620, 626, 632, 638,
	650, 656, 662, 668, 674
};

CPlayer *CheradPlayer::factory(Copl *newopl)
{
	return new CheradPlayer(newopl);
}

std::string CheradPlayer::gettype()
{
	char scomp[12 + 1] = "";
	if (comp > HERAD_COMP_NONE)
		sprintf(scomp, ", %s packed", (comp == HERAD_COMP_HSQ ? "HSQ" : "SQX"));
	char type[40 + 1];
	sprintf(type, "HERAD System %s (version %d%s)", (AGD ? "AGD" : "SDB"), (v2 ? 2 : 1), scomp);
	return std::string(type);
}

bool isHSQ(uint8_t * data, int size)
{
	// data[0] - word DecompSize
	// data[1]
	// data[2] - byte Null = 0
	// data[3] - word CompSize
	// data[4]
	// data[5] - byte Checksum
	if ( data[2] != 0 )
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: Is not HSQ, wrong check byte.\n");
		#endif
		return false;
	}
	if ( *(uint16_t *)(data + 3) != size )
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: Is not HSQ, wrong compressed size.\n");
		#endif
		return false;
	}
	uint8_t checksum = 0;
	for (int i = 0; i < HERAD_MIN_SIZE; i++)
	{
		checksum += data[i];
	}
	if ( checksum != 0xAB )
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: Is not HSQ, wrong checksum.\n");
		#endif
		return false;
	}
	return true;
}

bool isSQX(uint8_t * data)
{
	// data[0] - word OutbufInit
	// data[1]
	// data[2] - byte SQX flag #1
	// data[3] - byte SQX flag #2
	// data[4] - byte SQX flag #3
	// data[5] - byte CntOffPart
	if ( data[2] > 2 || data[3] > 2 || data[4] > 2 )
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: Is not SQX, wrong flags.\n");
		#endif
		return false;
	}
	if ( data[5] == 0 || data[5] > 15 )
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: Is not SQX, wrong bit count.\n");
		#endif
		return false;
	}
	return true;
}

int HSQ_decompress(uint8_t * data, int size, uint8_t * out)
{
	uint32_t queue = 1;
	int8_t bit;
	int offset, count;
	uint16_t out_size = *(uint16_t *)data;
	uint8_t * src = data;
	uint8_t * dst = out;

	src += 6;
	while (true)
	{
		// get next bit of the queue
		if (queue == 1)
		{
			queue = *(uint16_t *)src | 0x10000;
			src += 2;
		}
		bit = queue & 1;
		queue >>= 1;
		// if bit is non-zero
		if (bit)
		{
			// copy next byte of the input to the output
			*dst++ = *src++;
		}
		else
		{
			// get next bit of the queue
			if (queue == 1)
			{
				queue = *(uint16_t *)src | 0x10000;
				src += 2;
			}
			bit = queue & 1;
			queue >>= 1;
			// if bit is non-zero
			if (bit)
			{
				// count = next 3 bits of the input
				// offset = next 13 bits of the input minus 8192
				count = *(uint16_t *)src;
				offset = (count >> 3) - 8192;
				count &= 7;
				src += 2;
				// if count is zero
				if (!count)
				{
					// count = next 8 bits of the input
					count = *(uint8_t *)src;
					src++;
				}
				// if count is zero
				if (!count)
					break; // finish the unpacking
			}
			else
			{
				// count = next bit of the queue * 2 + next bit of the queue
				if (queue == 1)
				{
					queue = *(uint16_t *)src | 0x10000;
					src += 2;
				}
				bit = queue & 1;
				queue >>= 1;
				count = bit << 1;
				if (queue == 1)
				{
					queue = *(uint16_t *)src | 0x10000;
					src += 2;
				}
				bit = queue & 1;
				queue >>= 1;
				count += bit;
				// offset = next 8 bits of the input minus 256
				offset = *(uint8_t *)src;
				offset -= 256;
				src++;
			}
			count += 2;
			// copy count bytes at (output + offset) to the output
			while (count--)
			{
				*dst = *(dst + offset);
				dst++;
			}
		}
	}
	return out_size;
}

int SQX_decompress(uint8_t * data, int size, uint8_t * out)
{
	int16_t offset, count;
	uint8_t * src = data;
	uint8_t * dst = out;
	bool done = false;

	*(uint16_t *)dst = *(uint16_t *)src;
	src += 6;
	uint16_t u = 0xFFFF;
	uint16_t l = u;
	for (int i = 0; i < data[5]; i++)
	{
		u >>= 1;
		l <<= 1;
	}
	uint16_t mask = ~u | ~l;
	uint16_t queue = 1;
	int8_t bit, bit_p;
	while (true)
	{
		bit = queue & 1;
		queue >>= 1;
		if (queue == 0)
		{
			queue = *(uint16_t *)src;
			src += 2;
			bit_p = bit;
			bit = queue & 1;
			queue >>= 1;
			if (bit_p)
				queue |= 0x8000;
		}
		if (bit == 0)
		{
			switch (data[2])
			{
			case 0:
				*dst++ = *src++;
				break;
			case 1:
				count = 0;
				bit = queue & 1;
				queue >>= 1;
				if (queue == 0)
				{
					queue = *(uint16_t *)src;
					src += 2;
					bit_p = bit;
					bit = queue & 1;
					queue >>= 1;
					if (bit_p)
						queue |= 0x8000;
					count = bit;
					bit = queue & 1;
					queue >>= 1;
				}
				else
				{
					count = bit;
					bit = queue & 1;
					queue >>= 1;
					if (queue == 0)
					{
						queue = *(uint16_t *)src;
						src += 2;
						bit_p = bit;
						bit = queue & 1;
						queue >>= 1;
						if (bit_p)
							queue |= 0x8000;
					}
				}
				count = (count << 1) | bit;
				offset = *(uint8_t *)src;
				offset -= 256;
				src++;
				count += 2;
				while (count--)
				{
					*dst = *(dst + offset);
					dst++;
				}
				break;
			case 2:
				count = *(uint16_t *)src;
				offset = (count >> data[5]) | (mask & 0xFF00);
				count &= mask & 0xFF;
				src += 2;
				if (!count)
				{
					count = *(uint8_t *)src;
					src++;
				}
				if (!count)
				{
					done = true;
					break;
				}
				count += 2;
				while (count--)
				{
					*dst = *(dst + offset);
					dst++;
				}
				break;
			}
			if (done)
				break;
			continue;
		}
		else
		{
			bit = queue & 1;
			queue >>= 1;
			if (queue == 0)
			{
				queue = *(uint16_t *)src;
				src += 2;
				bit_p = bit;
				bit = queue & 1;
				queue >>= 1;
				if (bit_p)
					queue |= 0x8000;
			}
			if (bit == 0)
			{
				switch (data[3])
				{
				case 0:
					*dst++ = *src++;
					break;
				case 1:
					count = 0;
					bit = queue & 1;
					queue >>= 1;
					if (queue == 0)
					{
						queue = *(uint16_t *)src;
						src += 2;
						bit_p = bit;
						bit = queue & 1;
						queue >>= 1;
						if (bit_p)
							queue |= 0x8000;
						count = bit;
						bit = queue & 1;
						queue >>= 1;
					}
					else
					{
						count = bit;
						bit = queue & 1;
						queue >>= 1;
						if (queue == 0)
						{
							queue = *(uint16_t *)src;
							src += 2;
							bit_p = bit;
							bit = queue & 1;
							queue >>= 1;
							if (bit_p)
								queue |= 0x8000;
						}
					}
					count = (count << 1) | bit;
					offset = *(uint8_t *)src;
					offset -= 256;
					src++;
					count += 2;
					while (count--)
					{
						*dst = *(dst + offset);
						dst++;
					}
					break;
				case 2:
					count = *(uint16_t *)src;
					offset = (count >> data[5]) | (mask & 0xFF00);
					count &= mask & 0xFF;
					src += 2;
					if (!count)
					{
						count = *(uint8_t *)src;
						src++;
					}
					if (!count)
					{
						done = true;
						break;
					}
					count += 2;
					while (count--)
					{
						*dst = *(dst + offset);
						dst++;
					}
					break;
				}
				if (done)
					break;
				continue;
			}
			else
			{
				switch (data[4])
				{
				case 0:
					*dst++ = *src++;
					break;
				case 1:
					count = 0;
					bit = queue & 1;
					queue >>= 1;
					if (queue == 0)
					{
						queue = *(uint16_t *)src;
						src += 2;
						bit_p = bit;
						bit = queue & 1;
						queue >>= 1;
						if (bit_p)
							queue |= 0x8000;
						count = bit;
						bit = queue & 1;
						queue >>= 1;
					}
					else
					{
						count = bit;
						bit = queue & 1;
						queue >>= 1;
						if (queue == 0)
						{
							queue = *(uint16_t *)src;
							src += 2;
							bit_p = bit;
							bit = queue & 1;
							queue >>= 1;
							if (bit_p)
								queue |= 0x8000;
						}
					}
					count = (count << 1) | bit;
					offset = *(uint8_t *)src;
					offset -= 256;
					src++;
					count += 2;
					while (count--)
					{
						*dst = *(dst + offset);
						dst++;
					}
					break;
				case 2:
					count = *(uint16_t *)src;
					offset = (count >> data[5]) | (mask & 0xFF00);
					count &= mask & 0xFF;
					src += 2;
					if (!count)
					{
						count = *(uint8_t *)src;
						src++;
					}
					if (!count)
					{
						done = true;
						break;
					}
					count += 2;
					while (count--)
					{
						*dst = *(dst + offset);
						dst++;
					}
					break;
				}
				if (done)
					break;
				continue;
			}
		}
	}
	return dst - out;
}

bool CheradPlayer::load(const std::string &filename, const CFileProvider &fp)
{
	binistream *f = fp.open(filename); if(!f) return false;

	// file validation
	if (!fp.extension(filename, ".hsq") &&
		!fp.extension(filename, ".sqx") &&
		!fp.extension(filename, ".sdb") &&
		!fp.extension(filename, ".agd") &&
		!fp.extension(filename, ".ha2"))
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: Unsupported file extension.\n");
		#endif
		fp.close(f);
		return false;
	}
	int size = fp.filesize(f);
	if (size < HERAD_MIN_SIZE)
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: File size is too small.\n");
		#endif
		fp.close(f);
		return false;
	}
	if (size > HERAD_MAX_SIZE)
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: File size is too big.\n");
		#endif
		fp.close(f);
		return false;
	}
	// Read entire file into memory
	uint8_t * data = new uint8_t[size];
	f->readString((char *)data, size);
	fp.close(f);
	// Detect compression
	if (isHSQ(data, size))
	{
		comp = HERAD_COMP_HSQ;
		uint8_t * out = new uint8_t[HERAD_MAX_SIZE];
		memset(out, 0, HERAD_MAX_SIZE);
		size = HSQ_decompress(data, size, out);
		delete[] data;
		data = new uint8_t[size];
		memcpy(data, out, size);
		delete[] out;
	}
	else if (isSQX(data))
	{
		comp = HERAD_COMP_SQX;
		uint8_t * out = new uint8_t[HERAD_MAX_SIZE];
		memset(out, 0, HERAD_MAX_SIZE);
		size = SQX_decompress(data, size, out);
		delete[] data;
		data = new uint8_t[size];
		memcpy(data, out, size);
		delete[] out;
	}
	else
	{
		comp = HERAD_COMP_NONE;
	}
	// Process file header
	uint16_t offset;
	if (size < HERAD_HEAD_SIZE)
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: File size is too small.\n");
		#endif
		goto failure;
	}
	if ( size < *(uint16_t *)data )
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: Incorrect offset / file size.\n");
		#endif
		goto failure;
	}
	nInsts = (size - *(uint16_t *)data) / HERAD_INST_SIZE;
	if ( nInsts == 0 )
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: M32 files are not supported.\n");
		#endif
		goto failure;
	}
	offset = *(uint16_t *)(data + 2);
	if ( offset != 0x32 && offset != 0x52 )
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: Wrong first track offset.\n");
		#endif
		goto failure;
	}
	AGD = offset == 0x52;
	wLoopStart = *(uint16_t *)(data + 0x2C);
	wLoopEnd = *(uint16_t *)(data + 0x2E);
	wLoopCount = *(uint16_t *)(data + 0x30);
	wSpeed = *(uint16_t *)(data + 0x32);
	if (wSpeed == 0)
	{
		#ifdef DEBUG
		AdPlug_LogWrite("HERAD: Speed is not defined.\n");
		#endif
		goto failure;
	}
	nTracks = 0;
	for (int i = 0; i < HERAD_MAX_TRACKS; i++)
	{
		if ( *(uint16_t *)(data + 2 + i * 2) == 0 )
			break;
		nTracks++;
	}
	track = new herad_trk[nTracks];
	chn = new herad_chn[nTracks];
	for (int i = 0; i < nTracks; i++)
	{
		offset = *(uint16_t *)(data + 2 + i * 2) + 2;
		uint16_t next = (i < HERAD_MAX_TRACKS - 1 ? *(uint16_t *)(data + 2 + (i + 1) * 2) + 2 : *(uint16_t *)data);
		if (next <= 2) next = *(uint16_t *)data;

		track[i].size = next - offset;
		track[i].data = new uint8_t[track[i].size];
		memcpy(track[i].data, data + offset, track[i].size);
	}
	inst = new herad_inst[nInsts];
	offset = *(uint16_t *)data;
	v2 = true;
	for (int i = 0; i < nInsts; i++)
	{
		memcpy(inst[i].data, data + offset + i * HERAD_INST_SIZE, HERAD_INST_SIZE);
		if (v2 && inst[i].param.mode == HERAD_INSTMODE_SDB1)
			v2 = false;
	}
	delete[] data;
	goto good;

failure:
	delete[] data;
	return false;

good:
	rewind(0);
	return true;
}

void CheradPlayer::rewind(int subsong)
{
	wTime = 0;
	songend = false;

	for (int i = 0; i < nTracks; i++)
	{
		track[i].pos = 0;
		track[i].counter = 0;
		track[i].ticks = 0;
		chn[i].program = 0;
		chn[i].playprog = 0;
		chn[i].note = 24;
		chn[i].keyon = false;
		chn[i].bend = 0x40;
		chn[i].slide_sign = 0;
		chn[i].slide_dur = 0;
		chn[i].slide_coarse = false;
		chn[i].slide_step = 0;
	}

	opl->init();
	opl->write(1, 32); // Enable Waveform Select
	opl->write(0xBD, 0); // Disable Percussion Mode
	opl->write(8, 64); // Enable Note-Sel
	if (AGD)
	{
		opl->setchip(1);
		opl->write(5, 1); // Enable OPL3
		opl->write(4, 0); // Disable 4OP Mode
		opl->setchip(0);
	}
}

/*
 * Get delta ticks (t - track index)
 */
uint32_t CheradPlayer::GetTicks(uint8_t t)
{
	uint32_t result = 0;
	do
	{
		result <<= 7;
		result |= track[t].data[track[t].pos] & 0x7F;
	} while (track[t].data[track[t].pos++] & 0x80 && track[t].pos < track[t].size);
	return result;
}

/*
 * Execute event (t - track index)
 */
void CheradPlayer::executeCommand(uint8_t t)
{
	uint8_t status, note, par;
	int8_t macro;

	if (t >= nTracks)
		return;

	if (t >= (AGD ? 18 : 9))
	{
		track[t].pos = track[t].size;
		return;
	}

	// execute MIDI command
	status = track[t].data[track[t].pos++];
	if (status == 0xFF)
	{
		track[t].pos = track[t].size;
	}
	else
	{
		switch (status & 0xF0)
		{
		case 0x80:	// Note Off
			note = track[t].data[track[t].pos++];
			par = (v2 ? 0 : track[t].data[track[t].pos++]);
			if (note != chn[t].note || !chn[t].keyon)
				break;
			chn[t].keyon = false;
			playNote(t, note, HERAD_NOTE_OFF);
			break;
		case 0x90:	// Note On
			note = track[t].data[track[t].pos++];
			par = track[t].data[track[t].pos++];
			if (chn[t].keyon)
			{
				// turn off last active note
				chn[t].keyon = false;
				playNote(t, chn[t].note, HERAD_NOTE_OFF);
			}
			if (v2 && inst[chn[t].program].param.mode == HERAD_INSTMODE_KMAP)
			{
				// keymap is used
				int8_t mp = note - (inst[chn[t].program].keymap.offset + 24);
				if (mp < 0 || mp >= HERAD_INST_SIZE - 4)
					break; // if not in range, skip note
				chn[t].playprog = inst[chn[t].program].keymap.index[mp];
				changeProgram(t, chn[t].playprog);
			}
			chn[t].note = note;
			chn[t].keyon = true;
			chn[t].bend = 0x40;
			if (v2 && inst[chn[t].playprog].param.mode == HERAD_INSTMODE_KMAP)
				break;
			playNote(t, note, HERAD_NOTE_ON);
			macro = inst[chn[t].playprog].param.mc_mod_out_vel;
			if (macro != 0)
				macroModOutput(t, chn[t].playprog, macro, par);
			macro = inst[chn[t].playprog].param.mc_car_out_vel;
			if (macro != 0)
				macroCarOutput(t, chn[t].playprog, macro, par);
			macro = inst[chn[t].playprog].param.mc_fb_vel;
			if (macro != 0)
				macroFeedback(t, chn[t].playprog, macro, par);
			break;
		case 0xA0:	// Unused
		case 0xB0:	// Unused
			track[t].pos += 2;
			break;
		case 0xC0:	// Program Change
			par = track[t].data[track[t].pos++];
			if (par >= nInsts)
				break;
			chn[t].program = par;
			chn[t].playprog = par;
			changeProgram(t, par);
			break;
		case 0xD0:	// Aftertouch
			par = track[t].data[track[t].pos++];
			if (v2) // version 2 ignores this event
				break;
			macro = inst[chn[t].playprog].param.mc_mod_out_at;
			if (macro != 0)
				macroModOutput(t, chn[t].playprog, macro, par);
			macro = inst[chn[t].playprog].param.mc_car_out_at;
			if (macro != 0 && inst[chn[t].playprog].param.mc_car_out_vel != 0)
				macroCarOutput(t, chn[t].playprog, macro, par);
			macro = inst[chn[t].playprog].param.mc_fb_at;
			if (macro != 0)
				macroFeedback(t, chn[t].playprog, macro, par);
			break;
		case 0xE0:	// Pitch Bend
			par = track[t].data[track[t].pos++];
			chn[t].bend = par;
			if (chn[t].keyon) // update pitch
				playNote(t, chn[t].note, HERAD_NOTE_UPDATE);
			break;
		default:
			track[t].pos = track[t].size;
			break;
		}
	}
}

void CheradPlayer::clipNote(uint8_t * note, bool soft)
{
	// C2 ~ B9
	if (*note < 24)
		*note = 24;
	if (*note > 119)
		*note = (soft ? 119 : 24);
}

/*
 * Play Note (c - channel, note number, note state - see HERAD_NOTE_*)
 */
void CheradPlayer::playNote(uint8_t c, uint8_t note, uint8_t state)
{
	if (inst[chn[c].playprog].param.mc_transpose != 0)
		macroTranspose(&note, chn[c].playprog);
	uint8_t bend = chn[c].bend;
	if (bend != 0x40)
	{
		// normalize note and bend
		if (bend < 0x40)
		{
			while (bend <= 0x20)
			{
				note--;
				bend += 0x20;
			}
		}
		if (bend > 0x40)
		{
			while (bend >= 0x60)
			{
				note++;
				bend -= 0x20;
			}
		}
		// now bend in range 0x21..0x40..0x5F
	}
	clipNote(&note);
	uint8_t oct = note / 12 - 2;
	uint16_t freq = FNum[note % 12];
	if (bend != 0x40)
	{
		uint16_t diff;
		int8_t coef = bend - 0x40; // -31..+31
		if (note % 12 == 0 && bend < 0x40)
		{
			diff = freq - HERAD_FNUM_MIN;
			freq += diff * coef / 31;
		}
		else if (note % 12 == 11 && bend > 0x40)
		{
			diff = HERAD_FNUM_MAX - freq;
			freq += diff * coef / 31;
		}
		else
		{
			if (bend < 0x40)
			{
				diff = freq - FNum[(note - 1) % 12];
			}
			else
			{
				diff = FNum[(note + 1) % 12] - freq;
			}
			freq += diff * coef / 32;
		}
	}
	if (state != HERAD_NOTE_UPDATE)
	{
		chn[c].slide_sign = inst[chn[c].playprog].param.mc_slide_range;
		if (chn[c].slide_sign != 0)
		{
			chn[c].slide_dur = (state == HERAD_NOTE_ON ? inst[chn[c].playprog].param.mc_slide_dur : 0);
			chn[c].slide_coarse = (inst[chn[c].playprog].param.mc_slide_coarse & 1) > 0;
			chn[c].slide_step = 0;
		}
	}
	setFreq(c, oct, freq, state != HERAD_NOTE_OFF);
}

/*
 * Set Frequency and Key (c - channel, octave, frequency, note on)
 */
void CheradPlayer::setFreq(uint8_t c, uint8_t oct, uint16_t freq, bool on)
{
	uint8_t reg, val;

	if (c >= 9) opl->setchip(1);

	reg = 0xA0 + (c % 9);
	val = freq & 0xFF;
	opl->write(reg, val);
	reg = 0xB0 + (c % 9);
	val = ((freq >> 8) & 3) |
		((oct & 7) << 2) |
		((on ? 1 : 0) << 5);
	opl->write(reg, val);

	if (c >= 9) opl->setchip(0);
}

/*
 * Change Program (c - channel, i - instrument index)
 */
void CheradPlayer::changeProgram(uint8_t c, uint8_t i)
{
	uint8_t reg, val;

	if (v2 && inst[i].param.mode == HERAD_INSTMODE_KMAP)
		return;

	if (c >= 9) opl->setchip(1);

	// Amp Mod / Vibrato / EG type / Key Scaling / Multiple
	reg = 0x20 + slot_offset[c % 9];
	val = (inst[i].param.mod_mul & 15) |
		((inst[i].param.mod_ksr & 1) << 4) |
		((inst[i].param.mod_eg > 0 ? 1 : 0) << 5) |
		((inst[i].param.mod_vib & 1) << 6) |
		((inst[i].param.mod_am & 1) << 7);
	opl->write(reg, val);
	reg += 3;
	val = (inst[i].param.car_mul & 15) |
		((inst[i].param.car_ksr & 1) << 4) |
		((inst[i].param.car_eg > 0 ? 1 : 0) << 5) |
		((inst[i].param.car_vib & 1) << 6) |
		((inst[i].param.car_am & 1) << 7);
	opl->write(reg, val);

	// Key scaling level / Output level
	reg = 0x40 + slot_offset[c % 9];
	val = (inst[i].param.mod_out & 63) |
		((inst[i].param.mod_ksl & 3) << 6);
	opl->write(reg, val);
	reg += 3;
	val = (inst[i].param.car_out & 63) |
		((inst[i].param.car_ksl & 3) << 6);
	opl->write(reg, val);

	// Attack Rate / Decay Rate
	reg = 0x60 + slot_offset[c % 9];
	val = (inst[i].param.mod_D & 15) |
		((inst[i].param.mod_A & 15) << 4);
	opl->write(reg, val);
	reg += 3;
	val = (inst[i].param.car_D & 15) |
		((inst[i].param.car_A & 15) << 4);
	opl->write(reg, val);

	// Sustain Level / Release Rate
	reg = 0x80 + slot_offset[c % 9];
	val = (inst[i].param.mod_R & 15) |
		((inst[i].param.mod_S & 15) << 4);
	opl->write(reg, val);
	reg += 3;
	val = (inst[i].param.car_R & 15) |
		((inst[i].param.car_S & 15) << 4);
	opl->write(reg, val);

	// Panning / Feedback strength / Connection type
	reg = 0xC0 + (c % 9);
	val = (inst[i].param.con > 0 ? 0 : 1) |
		((inst[i].param.feedback & 7) << 1) |
		((AGD ? (inst[i].param.pan == 0 || inst[i].param.pan > 3 ? 3 : inst[i].param.pan) : 0) << 4);
	opl->write(reg, val);

	// Wave Select
	reg = 0xE0 + slot_offset[c % 9];
	val = inst[i].param.mod_wave & (AGD ? 7 : 3);
	opl->write(reg, val);
	reg += 3;
	val = inst[i].param.car_wave & (AGD ? 7 : 3);
	opl->write(reg, val);

	if (c >= 9) opl->setchip(0);
}

/*
 * Macro: Change Modulator Output (c - channel, i - instrument index, sensitivity, level)
 */
void CheradPlayer::macroModOutput(uint8_t c, uint8_t i, int8_t sens, uint8_t level)
{
	uint8_t reg, val;
	uint16_t output;

	if (sens < -4 || sens > 4)
		return;

	if (sens < 0)
	{
		output = (level >> (sens + 4) > 63 ? 63 : level >> (sens + 4));
	}
	else
	{
		output = ((0x80 - level) >> (4 - sens) > 63 ? 63 : (0x80 - level) >> (4 - sens));
	}
	output += inst[i].param.mod_out;
	if (output > 63) output = 63;

	if (c >= 9) opl->setchip(1);

	// Key scaling level / Output level
	reg = 0x40 + slot_offset[c % 9];
	val = (output & 63) |
		((inst[i].param.mod_ksl & 3) << 6);
	opl->write(reg, val);

	if (c >= 9) opl->setchip(0);
}

/*
 * Macro: Change Carrier Output (c - channel, i - instrument index, sensitivity, level)
 */
void CheradPlayer::macroCarOutput(uint8_t c, uint8_t i, int8_t sens, uint8_t level)
{
	uint8_t reg, val;
	uint16_t output;

	if (sens < -4 || sens > 4)
		return;

	if (sens < 0)
	{
		output = (level >> (sens + 4) > 63 ? 63 : level >> (sens + 4));
	}
	else
	{
		output = ((0x80 - level) >> (4 - sens) > 63 ? 63 : (0x80 - level) >> (4 - sens));
	}
	output += inst[i].param.car_out;
	if (output > 63) output = 63;

	if (c >= 9) opl->setchip(1);

	// Key scaling level / Output level
	reg = 0x43 + slot_offset[c % 9];
	val = (output & 63) |
		((inst[i].param.car_ksl & 3) << 6);
	opl->write(reg, val);

	if (c >= 9) opl->setchip(0);
}

/*
 * Macro: Change Feedback (c - channel, i - instrument index, sensitivity, level)
 */
void CheradPlayer::macroFeedback(uint8_t c, uint8_t i, int8_t sens, uint8_t level)
{
	uint8_t reg, val;
	uint8_t feedback;

	if (sens < -6 || sens > 6)
		return;

	if (sens < 0)
	{
		feedback = (level >> (sens + 7) > 7 ? 7 : level >> (sens + 7));
	}
	else
	{
		feedback = ((0x80 - level) >> (7 - sens) > 7 ? 7 : (0x80 - level) >> (7 - sens));
	}
	feedback += inst[i].param.feedback;
	if (feedback > 7) feedback = 7;

	if (c >= 9) opl->setchip(1);

	// Panning / Feedback strength / Connection type
	reg = 0xC0 + (c % 9);
	val = (inst[i].param.con > 0 ? 0 : 1) |
		((feedback & 7) << 1) |
		((AGD ? (inst[i].param.pan == 0 || inst[i].param.pan > 3 ? 3 : inst[i].param.pan) : 0) << 4);
	opl->write(reg, val);

	if (c >= 9) opl->setchip(0);
}

/*
 * Macro: Root Note Transpose (note, i - instrument index)
 */
void CheradPlayer::macroTranspose(uint8_t * note, uint8_t i)
{
	uint8_t tran = inst[i].param.mc_transpose;
	uint8_t diff = (tran - 0x31) & 0xFF;
	if (v2 && diff < 0x60)
		*note = (diff + 0x18) & 0xFF;
	else
		*note = (*note + tran) & 0xFF;
}

/*
 * Macro: Pitch Bend Slide (c - channel)
 */
void CheradPlayer::macroSlide(uint8_t c)
{
	if (chn[c].slide_sign < 0)
		chn[c].slide_step--;
	else
		chn[c].slide_step++;
	uint8_t note = chn[c].note;
	if (inst[chn[c].playprog].param.mc_transpose != 0)
		macroTranspose(&note, chn[c].playprog);
	clipNote(&note, false);
	uint8_t oct = note / 12 - 2;
	uint16_t freq = FNum[note % 12];
	int32_t scaleX;
	int16_t scale_oct;
	if (chn[c].slide_coarse)
	{
		scaleX = ((note % 12) * 5) + chn[c].slide_step;
		scale_oct = scaleX / (12 * 5);
		if (oct + scale_oct < 0 || oct + scale_oct > 7)
		{
			chn[c].slide_dur = 0;
			return;
		}
		oct += scale_oct;
		freq = FNum_coarse[scaleX % (12 * 5)];
	}
	else // fine tune
	{
		scaleX = freq - HERAD_FNUM_MIN + chn[c].slide_step;
		scale_oct = scaleX / (HERAD_FNUM_MAX - HERAD_FNUM_MIN + 1);
		if (oct + scale_oct < 0 || oct + scale_oct > 7)
		{
			chn[c].slide_dur = 0;
			return;
		}
		oct += scale_oct;
		freq = (scaleX % (HERAD_FNUM_MAX - HERAD_FNUM_MIN + 1)) + HERAD_FNUM_MIN;
	}
	setFreq(c, oct, freq, chn[c].keyon);
	chn[c].slide_dur--;
}

bool CheradPlayer::update()
{
	songend = true;
	wTime = wTime - 256;
	for (uint8_t i = 0; i < nTracks; i++)
	{
		if (track[i].pos >= track[i].size)
			continue;
		songend = false; // track is not finished
		if (wTime < 0)
		{
			if (chn[i].slide_dur > 0 && chn[i].keyon)
				macroSlide(i);
			if (!track[i].counter)
			{
				bool first = track[i].pos == 0;
				track[i].ticks = GetTicks(i);
				if (first && track[i].ticks)
					track[i].ticks++; // workaround to synchronize tracks (there's always 1 excess tick at start)
			}
			if (++track[i].counter >= track[i].ticks)
			{
				track[i].counter = 0;
				while (track[i].pos < track[i].size)
				{
					executeCommand(i);
					if (track[i].pos >= track[i].size) {
						break;
					}
					else if (!track[i].data[track[i].pos]) // if next delay is zero
					{
						track[i].pos++;
					}
					else break;
				}
			}
			else if (track[i].ticks >= 0x8000)
			{
				track[i].pos = track[i].size;
				track[i].counter = track[i].ticks;
			}
		}
	}
	if (wTime < 0)
		wTime = wTime + wSpeed;
	return !songend;
}