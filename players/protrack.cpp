/*
 * protrack.cpp - Generic Protracker Player by Simon Peter (dn.tlp@gmx.net)
 *
 * NOTES:
 * Effect commands:
 * ----------------
 *   0xy	Arpeggio						xy=1st note,2nd note	[0-F]
 *   1xx	Frequency slide up				xx=sliding speed		[0-FF]
 *   2xx	Frequency slide down			xx=sliding speed		[0-FF]
 *   3xx	Tone portamento					xx=sliding speed		[0-FF]
 *   4xy	Vibrato							xx=speed,depth			[0-F]
 *   5xy	Tone portamento & volume slide	xy=vol up|vol down		[0-FF]
 *   6xy	Vibrato & volume slide			xy=vol up|vol down		[0-FF]
 *   7xx	Set tempo						xx=new tempo			[0-FF]
 *   8--	Release sustaining note
 *   9xy	Set carrier/modulator volume	xy=car vol|mod vol		[0-F]
 *  10xy	SA2 volume slide				xy=vol up|vol down		[0-F]
 *  11xx	Position jump					xx=new position			[0-FF]
 *  12xx	Set carr. & mod. volume			xx=new volume			[0-3F]
 *  13xx	Pattern break					xx=new row				[0-FF]
 *  14??	Extended command:
 *    0x		Set chip tremolo				x=new depth		[0-1]
 *    1x		Set chip vibrato				x=new depth		[0-1]
 *    3x		Retrig note						x=retrig speed	[0-F]
 *    4x		Fine volume slide up			x=vol up		[0-F]
 *    5x		Fine volume slide down			x=vol down		[0-F]
 *    6x		Fine frequency slide up			x=freq up		[0-F]
 *    7x		Fine frequency slide down		x=freq down		[0-F]
 *  15xx	SA2 set speed					xx=new speed			[0-FF]
 *  16xy	AMD volume slide				xy=vol up|vol down		[0-F]
 *  17xx	Set instrument volume			xx=new volume			[0-3F]
 *  18xx	AMD set speed					xx=new speed			[0-FF]
 *  19xx	RAD set speed					xx=new speed			[0-FF]
 *  20xx	RAD volume slide				xx=vol up/down			[0-FF]
 *  21xx	Set modulator volume			xx=new volume			[0-3F]
 *  22xx	Set carrier volume				xx=new volume			[0-3F]
 *  23xx	Fine frequency slide up			xx=sliding speed		[0-FF]
 *  24xx	Fine frequency slide down		xx=sliding speed		[0-FF]
 *  25xy	Set carrier/modulator waveform	xy=carr wav|mod wav		[0-3,F]
 *  26xy	Volume slide					xy=vol up|vol down		[0-F]
 * 255--	No operation (NOP)
 *
 * Special arpeggio commands:
 * --------------------------
 * 252: Set carr. & mod. volume
 * 253: Release sustaining note
 * 254: Arpeggio loop
 * 255: End of special arpeggio
 *
 * Instrument data (inst[].data[11]) values:
 * -----------------------------------------
 *  0 = (Channel)	Feedback strength / Connection type						(reg 0xc0)
 *  1 = (Modulator)	Amp Mod / Vibrato / EG type / Key Scaling / Multiple	(reg 0x20)
 *  2 = (Carrier)	Amp Mod / Vibrato / EG type / Key Scaling / Multiple	(reg 0x23)
 *  3 = (Modulator)	Attack Rate / Decay Rate								(reg 0x60)
 *  4 = (Carrier)	Attack Rate / Decay Rate								(reg 0x63)
 *  5 = (Modulator)	Sustain Level / Release Rate							(reg 0x80)
 *  6 = (Carrier)	Sustain Level / Release Rate							(reg 0x83)
 *  7 = (Modulator)	Wave Select												(reg 0xe0)
 *  8 = (Carrier)	Wave Select												(reg 0xe3)
 *  9 = (Modulator)	Key scaling level / Operator output level				(reg 0x40)
 * 10 = (Carrier)	Key scaling level / Operator output level				(reg 0x43)
 */

#include "protrack.h"

static const unsigned short notetable[12] =		// SAdT2 adlib note table
			{340,363,385,408,432,458,485,514,544,577,611,647};

static const unsigned char vibratotab[32] =		// vibrato rate table
			{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1};

/*** public methods *************************************/

CmodPlayer::CmodPlayer(Copl *newopl): CPlayer(newopl), initspeed(6), flags(0)
{
	memset(inst,0,sizeof(inst));
	memset(trackord,0,64*9*2);
}

bool CmodPlayer::update()
{
	unsigned char pattbreak=0,donote;						// remember vars
	unsigned char pattnr,chan,row,info1,info2,info;			// cache vars
	unsigned short track;

	if(!speed)		// song full stop
		return !songend;

	// effect handling (timer dependant)
	for(chan=0;chan<9;chan++) {
		if(inst[channel[chan].inst].arpstart)	// special arpeggio
			if(channel[chan].arpspdcnt)
				channel[chan].arpspdcnt--;
			else
				if(arpcmd[channel[chan].arppos] != 255) {
					switch(arpcmd[channel[chan].arppos]) {
					case 252: channel[chan].vol1 = arplist[channel[chan].arppos];	// set volume
							if(channel[chan].vol1 > 63)	// ?????
								channel[chan].vol1 = 63;
							channel[chan].vol2 = channel[chan].vol1;
							setvolume(chan);
							break;
					case 253: channel[chan].key = 0; setfreq(chan); break;	// release sustaining note
					case 254: channel[chan].arppos = arplist[channel[chan].arppos]; break; // arpeggio loop
					default: if(arpcmd[channel[chan].arppos]) {
								if(arpcmd[channel[chan].arppos] / 10)
									opl->write(0xe3 + op_table[chan], arpcmd[channel[chan].arppos] / 10 - 1);
								if(arpcmd[channel[chan].arppos] % 10)
									opl->write(0xe0 + op_table[chan], (arpcmd[channel[chan].arppos] % 10) - 1);
								if(arpcmd[channel[chan].arppos] < 10)	// ?????
									opl->write(0xe0 + op_table[chan], arpcmd[channel[chan].arppos] - 1);
							 }
					}
					if(arpcmd[channel[chan].arppos] != 252) {
						if(arplist[channel[chan].arppos] <= 96)
							setnote(chan,channel[chan].note + arplist[channel[chan].arppos]);
						if(arplist[channel[chan].arppos] >= 100)
							setnote(chan,arplist[channel[chan].arppos] - 100);
					} else
						setnote(chan,channel[chan].note);
					setfreq(chan);
					if(arpcmd[channel[chan].arppos] != 255)
						channel[chan].arppos++;
					channel[chan].arpspdcnt = inst[channel[chan].inst].arpspeed - 1;
				}

		info1 = channel[chan].info1;
		info2 = channel[chan].info2;
		if(flags & MOD_FLAGS_DECIMAL)
			info = channel[chan].info1 * 10 + channel[chan].info2;
		else
			info = (channel[chan].info1 << 4) + channel[chan].info2;
		switch(channel[chan].fx) {
		case 0:	if(info) {									// arpeggio
					if(channel[chan].trigger < 2)
						channel[chan].trigger++;
					else
						channel[chan].trigger = 0;
					switch(channel[chan].trigger) {
					case 0: setnote(chan,channel[chan].note); break;
					case 1: setnote(chan,channel[chan].note + info1); break;
					case 2: setnote(chan,channel[chan].note + info2);
					}
					setfreq(chan);
				}
				break;
		case 1: slide_up(chan,info); setfreq(chan); break;	// slide up
		case 2: slide_down(chan,info); setfreq(chan); break;	// slide down
		case 3: tone_portamento(chan,channel[chan].portainfo); break;	// tone portamento
		case 4: vibrato(chan,channel[chan].vibinfo1,channel[chan].vibinfo2); break;	// vibrato
		case 5:												// tone portamento & volume slide
		case 6: if(channel[chan].fx == 5)				// vibrato & volume slide
					tone_portamento(chan,channel[chan].portainfo);
				else
					vibrato(chan,channel[chan].vibinfo1,channel[chan].vibinfo2);
		case 10: if(del % 4)	// SA2 volume slide
					break;
				 if(info1)
					 vol_up(chan,info1);
				 else
					 vol_down(chan,info2);
				 setvolume(chan);
				 break;
		case 14: if(info1 == 3)							// retrig note
					if(!(del % (info2+1)))
						playnote(chan);
				 break;
		case 16: if(del % 4)	// AMD volume slide
					break;
				 if(info1)
					 vol_up_alt(chan,info1);
				 else
					 vol_down_alt(chan,info2);
				 setvolume(chan);
				 break;
		case 20:				// RAD volume slide
			if(info < 50)
				vol_down_alt(chan,info);
			else
				vol_up_alt(chan,info - 50);
			setvolume(chan);
			break;
		case 26: // volume slide
			if(info1)
				vol_up(chan,info1);
			else
				vol_down(chan,info2);
			setvolume(chan);
			break;
		}
	}

	if(del) {		// speed compensation
		del--;
		return !songend;
	}

	// arrangement handling
	if(ord >= length) {
		songend = 1;				// set end-flag
		ord = restartpos;
	}
	pattnr = order[ord];

	// play row
	row = rw;
	for(chan=0;chan<9;chan++) {
		if(!(activechan >> (15 - chan)) & 1)	// channel active?
			continue;
		if(!(track = trackord[pattnr][chan]))	// resolve track
			continue;
		else
			track--;

		donote = 0;
		if(tracks[track][row].inst) {
			channel[chan].inst = tracks[track][row].inst - 1;
			channel[chan].vol1 = 63 - (inst[channel[chan].inst].data[10] & 63);
			channel[chan].vol2 = 63 - (inst[channel[chan].inst].data[9] & 63);
			setvolume(chan);
		}
		if(tracks[track][row].note && tracks[track][row].command != 3) {	// no tone portamento
			channel[chan].note = tracks[track][row].note;
			setnote(chan,tracks[track][row].note);
			channel[chan].nextfreq = channel[chan].freq;
			channel[chan].nextoct = channel[chan].oct;
			channel[chan].arppos = inst[channel[chan].inst].arpstart;
			channel[chan].arpspdcnt = 0;
			if(tracks[track][row].note != 127)	// handle key off
				donote = 1;
		}
		channel[chan].fx = tracks[track][row].command;
		channel[chan].info1 = tracks[track][row].param1;
		channel[chan].info2 = tracks[track][row].param2;

		if(donote)
			playnote(chan);

		// command handling (row dependant)
		info1 = channel[chan].info1;
		info2 = channel[chan].info2;
		if(flags & MOD_FLAGS_DECIMAL)
			info = channel[chan].info1 * 10 + channel[chan].info2;
		else
			info = (channel[chan].info1 << 4) + channel[chan].info2;
		switch(channel[chan].fx) {
		case 3:	if(tracks[track][row].note) {					// tone portamento
					if(tracks[track][row].note < 13)
						channel[chan].nextfreq = notetable[tracks[track][row].note - 1];
					else
						if(tracks[track][row].note % 12 > 0)
							channel[chan].nextfreq = notetable[(tracks[track][row].note % 12) - 1];
						else
							channel[chan].nextfreq = notetable[11];
					channel[chan].nextoct = (tracks[track][row].note - 1) / 12;
					if(tracks[track][row].note == 127) {	// handle key off
						channel[chan].nextfreq = channel[chan].freq;
						channel[chan].nextoct = channel[chan].oct;
					}
				}
				if(info)		// remember vars
					channel[chan].portainfo = info;
				break;
		case 4: if(info) {										// vibrato (remember vars)
					channel[chan].vibinfo1 = info1;
					channel[chan].vibinfo2 = info2;
				}
				break;
		case 7: tempo = info; break;							// set tempo
		case 8: channel[chan].key = 0; setfreq(chan); break;	// release sustaining note
		case 9: // set carrier/modulator volume
				if(info1)
					channel[chan].vol1 = info1 * 7;
				else
					channel[chan].vol2 = info2 * 7;
				setvolume(chan);
				break;
		case 11: pattbreak = 1; rw = 0; if(info < ord) songend = 1; ord = info; break; // position jump
		case 12: // set volume
				channel[chan].vol1 = info;
				channel[chan].vol2 = info;
				if(channel[chan].vol1 > 63)
					channel[chan].vol1 = 63;
				if(channel[chan].vol2 > 63)
					channel[chan].vol2 = 63;
				setvolume(chan);
				break;
		case 13: if(!pattbreak) { pattbreak = 1; rw = info; ord++; } break;	// pattern break
		case 14: // extended command
				switch(info1) {
				case 0: if(info2)								// define cell-tremolo
							regbd |= 128;
						else
							regbd &= 127;
						opl->write(0xbd,regbd);
						break;
				case 1: if(info2)								// define cell-vibrato
							regbd |= 64;
						else
							regbd &= 191;
						opl->write(0xbd,regbd);
						break;
				case 4: vol_up_alt(chan,info2);					// increase volume fine
						setvolume(chan);
						break;
				case 5: vol_down_alt(chan,info2);				// decrease volume fine
						setvolume(chan);
						break;
				case 6: slide_up(chan,info2);					// manual slide up
						setfreq(chan);
						break;
				case 7: slide_down(chan,info2);					// manual slide down
						setfreq(chan);
						break;
				}
				break;
		case 15: // SA2 set speed
			if(info <= 0x1f)
				speed = info;
			if(info >= 0x32 && info <= 0xff)
				tempo = info;
			if(!info)
				songend = 1;
			break;
		case 17: // alternate set volume
			channel[chan].vol1 = info;
			if(channel[chan].vol1 > 63)
				channel[chan].vol1 = 63;
			if(inst[channel[chan].inst].data[0] & 1) {
				channel[chan].vol2 = info;
	 			if(channel[chan].vol2 > 63)
					channel[chan].vol2 = 63;
			}
			setvolume(chan);
			break;
		case 18: // AMD set speed
			if(info <= 31 && info > 0)
				speed = info;
			if(info > 31 || !info)
				tempo = info;
			break;
		case 19: // RAD/A2M set speed
			speed = info ? info : info + 1;
			break;
		case 21: // set modulator volume
			if(info <= 63)
				channel[chan].vol2 = info;
			else
				channel[chan].vol2 = 63;
			setvolume(chan);
			break;
		case 22: // set carrier volume
			if(info <= 63)
				channel[chan].vol1 = info;
			else
				channel[chan].vol1 = 63;
			setvolume(chan);
			break;
		case 23: // fine frequency slide up
			slide_up(chan,info);
			setfreq(chan);
			break;
		case 24: // fine frequency slide down
			slide_down(chan,info);
			setfreq(chan);
			break;
		case 25: // set carrier/modulator waveform
			if(info1 != 0x0f)
				opl->write(0xe3 + op_table[chan],info1);
			if(info2 != 0x0f)
				opl->write(0xe0 + op_table[chan],info2);
			break;
		}
	}

	del = speed - 1;	// speed compensation
	if(!pattbreak) {			// next row (only if no manual advance)
		rw++;
		if(rw > 63) {
			rw = 0;
			ord++;
		}
	}
	if(order[ord] >= 0x80) {	// jump to order
		ord = order[ord] - 0x80;
		songend = 1;
	}

	return !songend;
}

void CmodPlayer::rewind(unsigned int subsong)
{
	songend = 0; del = 0; tempo = bpm; speed = initspeed;
	ord = 0; rw = 0; regbd = 0;

	memset(channel,0,sizeof(channel));
	opl->init();				// reset OPL chip
	opl->write(1,32);			// Go to ym3812 mode
}

float CmodPlayer::getrefresh()
{
	return (float) (tempo / 2.5);
}

/*** private methods *************************************/

void CmodPlayer::setvolume(unsigned char chan)
{
	opl->write(0x40 + op_table[chan], 63-channel[chan].vol2 + (inst[channel[chan].inst].data[9] & 192));
	opl->write(0x43 + op_table[chan], 63-channel[chan].vol1 + (inst[channel[chan].inst].data[10] & 192));
}

void CmodPlayer::setfreq(unsigned char chan)
{
	opl->write(0xa0 + chan, channel[chan].freq & 255);
	if(channel[chan].key)
		opl->write(0xb0 + chan, ((channel[chan].freq & 768) >> 8) + (channel[chan].oct << 2) | 32);
	else
		opl->write(0xb0 + chan, ((channel[chan].freq & 768) >> 8) + (channel[chan].oct << 2));
}

void CmodPlayer::playnote(unsigned char chan)
{
	unsigned char op = op_table[chan], insnr = channel[chan].inst;

	opl->write(0xb0 + chan, 0);	// stop old note

	// set instrument data
	opl->write(0x20 + op, inst[insnr].data[1]);
	opl->write(0x23 + op, inst[insnr].data[2]);
	opl->write(0x60 + op, inst[insnr].data[3]);
	opl->write(0x63 + op, inst[insnr].data[4]);
	opl->write(0x80 + op, inst[insnr].data[5]);
	opl->write(0x83 + op, inst[insnr].data[6]);
	opl->write(0xe0 + op, inst[insnr].data[7]);
	opl->write(0xe3 + op, inst[insnr].data[8]);
	opl->write(0xc0 + chan, inst[insnr].data[0]);
	opl->write(0xbd, inst[insnr].misc);	// set misc. register

	// set frequency, volume & play
	channel[chan].key = 1;
	setfreq(chan);
	setvolume(chan);
}

void CmodPlayer::setnote(unsigned char chan, int note)
{
	if(note > 96)
		if(note == 127) {	// key off
			channel[chan].key = 0;
			setfreq(chan);
			return;
		} else
			note = 96;

	if(note < 13)
		channel[chan].freq = notetable[note - 1];
	else
		if(note % 12 > 0)
			channel[chan].freq = notetable[(note % 12) - 1];
		else
			channel[chan].freq = notetable[11];
	channel[chan].oct = (note - 1) / 12;
	channel[chan].freq += inst[channel[chan].inst].slide;	// apply pre-slide
}

void CmodPlayer::slide_down(unsigned char chan, int amount)
{
	channel[chan].freq -= amount;
	if(channel[chan].freq <= 342)
		if(channel[chan].oct) {
			channel[chan].oct--;
			channel[chan].freq <<= 1;
		} else
			channel[chan].freq = 342;
}

void CmodPlayer::slide_up(unsigned char chan, int amount)
{
	channel[chan].freq += amount;
	if(channel[chan].freq >= 686)
		if(channel[chan].oct < 7) {
			channel[chan].oct++;
			channel[chan].freq >>= 1;
		} else
			channel[chan].freq = 686;
}

void CmodPlayer::tone_portamento(unsigned char chan, unsigned char info)
{
	if(channel[chan].freq + (channel[chan].oct << 10) < channel[chan].nextfreq +
		(channel[chan].nextoct << 10)) {
		slide_up(chan,info);
		if(channel[chan].freq + (channel[chan].oct << 10) > channel[chan].nextfreq +
		(channel[chan].nextoct << 10)) {
			channel[chan].freq = channel[chan].nextfreq;
			channel[chan].oct = channel[chan].nextoct;
		}
	}
	if(channel[chan].freq + (channel[chan].oct << 10) > channel[chan].nextfreq +
		(channel[chan].nextoct << 10)) {
		slide_down(chan,info);
		if(channel[chan].freq + (channel[chan].oct << 10) < channel[chan].nextfreq +
		(channel[chan].nextoct << 10)) {
			channel[chan].freq = channel[chan].nextfreq;
			channel[chan].oct = channel[chan].nextoct;
		}
	}
	setfreq(chan);
}

void CmodPlayer::vibrato(unsigned char chan, unsigned char speed, unsigned char depth)
{
	int i;

	if(!speed || !depth)
		return;

	if(depth > 14)
		depth = 14;

	for(i=0;i<speed;i++) {
		channel[chan].trigger++;
		while(channel[chan].trigger >= 64)
			channel[chan].trigger -= 64;
		if(channel[chan].trigger >= 16 && channel[chan].trigger < 48)
			slide_down(chan,vibratotab[channel[chan].trigger - 16] / (16-depth));
		if(channel[chan].trigger < 16)
			slide_up(chan,vibratotab[channel[chan].trigger + 16] / (16-depth));
		if(channel[chan].trigger >= 48)
			slide_up(chan,vibratotab[channel[chan].trigger - 48] / (16-depth));
	}
	setfreq(chan);
}

void CmodPlayer::vol_up(unsigned char chan, int amount)
{
	if(channel[chan].vol1 + amount < 63)
		channel[chan].vol1 += amount;
	else
		channel[chan].vol1 = 63;

	if(channel[chan].vol2 + amount < 63)
		channel[chan].vol2 += amount;
	else
		channel[chan].vol2 = 63;
}

void CmodPlayer::vol_down(unsigned char chan, int amount)
{
	if(channel[chan].vol1 - amount > 0)
		channel[chan].vol1 -= amount;
	else
		channel[chan].vol1 = 0;

	if(channel[chan].vol2 - amount > 0)
		channel[chan].vol2 -= amount;
	else
		channel[chan].vol2 = 0;
}

void CmodPlayer::vol_up_alt(unsigned char chan, int amount)
{
	if(channel[chan].vol1 + amount < 63)
		channel[chan].vol1 += amount;
	else
		channel[chan].vol1 = 63;
	if(inst[channel[chan].inst].data[0] & 1)
		if(channel[chan].vol2 + amount < 63)
			channel[chan].vol2 += amount;
		else
			channel[chan].vol2 = 63;
}

void CmodPlayer::vol_down_alt(unsigned char chan, int amount)
{
	if(channel[chan].vol1 - amount > 0)
		channel[chan].vol1 -= amount;
	else
		channel[chan].vol1 = 0;
	if(inst[channel[chan].inst].data[0] & 1)
		if(channel[chan].vol2 - amount > 0)
			channel[chan].vol2 -= amount;
		else
			channel[chan].vol2 = 0;
}
