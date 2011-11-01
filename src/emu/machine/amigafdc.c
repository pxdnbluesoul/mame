/****************************************************f***********************

    Amiga floppy disk controller emulation

***************************************************************************/


#include "emu.h"
#include "includes/amiga.h"
#include "formats/ami_dsk.h"
#include "formats/hxcmfm_dsk.h"
#include "formats/ipf_dsk.h"
#include "formats/mfi_dsk.h"
#include "amigafdc.h"
#include "machine/6526cia.h"

const device_type AMIGA_FDC = &device_creator<amiga_fdc>;

const floppy_format_type amiga_fdc::floppy_formats[] = {
	FLOPPY_ADF_FORMAT, FLOPPY_MFM_FORMAT, FLOPPY_IPF_FORMAT, FLOPPY_MFI_FORMAT,
	NULL
};


amiga_fdc::amiga_fdc(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock) :
	device_t(mconfig, AMIGA_FDC, "Amiga FDC", tag, owner, clock)
{
}

void amiga_fdc::device_start()
{
	floppy_devices[0] = machine().device<floppy_image_device>("fd0");
	floppy_devices[1] = machine().device<floppy_image_device>("fd1");
	floppy_devices[2] = machine().device<floppy_image_device>("fd2");
	floppy_devices[3] = machine().device<floppy_image_device>("fd3");
	floppy = 0;

	t_gen = timer_alloc(0);
}


void amiga_fdc::device_reset()
{
	floppy = 0;
	dsklen = 0x4000;
	dsksync = 0x4489;
	adkcon = 0;
	dmacon = 0;
	dskpt = 0;
	dsklen = 0x4000;
	pre_dsklen = 0x4000;
	dma_value = 0;
	dma_state = DMA_IDLE;

	live_abort();
}

void amiga_fdc::live_start()
{
	cur_live.tm = machine().time();
	cur_live.state = RUNNING;
	cur_live.next_state = -1;
	cur_live.shift_reg = 0;
	cur_live.bit_counter = 0;
	cur_live.pll.reset(cur_live.tm);
	cur_live.pll.set_clock(clocks_to_attotime(1));
	checkpoint_live = cur_live;

	live_run();
}

void amiga_fdc::checkpoint()
{
	checkpoint_live = cur_live;
}

void amiga_fdc::rollback()
{
	cur_live = checkpoint_live;
}

void amiga_fdc::live_delay(int state)
{
	cur_live.next_state = state;
	t_gen->adjust(cur_live.tm - machine().time());
}

void amiga_fdc::live_sync()
{
	if(!cur_live.tm.is_never()) {
		if(cur_live.tm > machine().time()) {
			rollback();
			live_run(machine().time());
		} else {
			if(cur_live.next_state != -1)
				cur_live.state = cur_live.next_state;
			if(cur_live.state == IDLE)
				cur_live.tm = attotime::never;
		}
		cur_live.next_state = -1;
		checkpoint();
	}
}

void amiga_fdc::live_abort()
{
	cur_live.tm = attotime::never;
	cur_live.state = IDLE;
	cur_live.next_state = -1;
}

void amiga_fdc::live_run(attotime limit)
{
	if(cur_live.state == IDLE || cur_live.next_state != -1)
		return;

	for(;;) {
		switch(cur_live.state) {
		case RUNNING: {
			int bit = cur_live.pll.get_next_bit(cur_live.tm, floppy, limit);
			if(bit < 0)
				return;
			cur_live.shift_reg = (cur_live.shift_reg << 1) | bit;
			cur_live.bit_counter++;

			if((adkcon & 0x0200) && !(cur_live.shift_reg & 0x80))
				cur_live.bit_counter--;

			if(cur_live.bit_counter == 8) {
				live_delay(RUNNING_SYNCPOINT);
				return;
			}
			if(dskbyt & 0x0400) {
				if(cur_live.shift_reg != dsksync) {
					live_delay(RUNNING_SYNCPOINT);
					return;
				}
			} else {
				if(cur_live.shift_reg == dsksync) {
					live_delay(RUNNING_SYNCPOINT);
					return;
				}
			}
			break;
		}

		case RUNNING_SYNCPOINT: {
			if(cur_live.shift_reg == dsksync) {
				if(adkcon & 0x0400) {
					if(dma_state == DMA_WAIT_START)
						dma_state = DMA_RUNNING_BYTE_0;

					cur_live.bit_counter = 0;
				}
				dskbyt |= 0x0400;
				address_space *space = machine().device("maincpu")->memory().space(AS_PROGRAM);
				amiga_custom_w(space, REG_INTREQ, 0x8000 | INTENA_DSKSYN, 0xffff);
			} else
				dskbyt &= ~0x0400;
			
			if(cur_live.bit_counter == 8) {
				dskbyt = (dskbyt & 0xff00) | 0x8000 | (cur_live.shift_reg & 0xff);
				cur_live.bit_counter = 0;

				switch(dma_state) {
				case DMA_IDLE:
				case DMA_WAIT_START:
					break;

				case DMA_RUNNING_BYTE_0:
					dma_value = (cur_live.shift_reg & 0xff) << 8;
					dma_state = DMA_RUNNING_BYTE_1;
					break;

				case DMA_RUNNING_BYTE_1: {
					dma_value |= cur_live.shift_reg & 0xff;

					amiga_state *state = machine().driver_data<amiga_state>();
					(*state->m_chip_ram_w)(state, dskpt, dma_value);

					dskpt += 2;
					dsklen--;
					if(dsklen & 0x3fff)
						dma_state = DMA_RUNNING_BYTE_0;
					else {
						dma_state = DMA_IDLE;
						address_space *space = machine().device("maincpu")->memory().space(AS_PROGRAM);
						amiga_custom_w(space, REG_INTREQ, 0x8000 | INTENA_DSKBLK, 0xffff);
					}
					break;
				}
				}
			}

			cur_live.state = RUNNING;
			checkpoint();
			break;
		}
		}
	}
}

bool amiga_fdc::dma_enabled()
{
	return (dsklen & 0x8000) && ((dmacon & 0x0210) == 0x0210);
}

void amiga_fdc::dma_check()
{
	if(dma_enabled() && (dsklen & 0x3fff)) {
		if(dma_state == IDLE)
			dma_state = adkcon & 0x0400 ? DMA_WAIT_START : DMA_RUNNING_BYTE_0;
	} else
		dma_state = IDLE;
}

void amiga_fdc::adkcon_set(UINT16 data)
{
	live_sync();
	adkcon = data;
}

void amiga_fdc::dsklen_w(UINT16 data)
{
	live_sync();
	if(!(data & 0x8000) || (data == pre_dsklen)) {
		dsklen = pre_dsklen = data;
		dma_check();

		dskbyt = dskbyt & 0x9fff;
		if(data & 0x4000)
			dskbyt |= 0x2000;
		if(dma_state != DMA_IDLE)
			dskbyt |= 0x4000;
	} else
		pre_dsklen = data;
}

void amiga_fdc::dskpth_w(UINT16 data)
{
	live_sync();
	dskpt = (dskpt & 0xffff) | (data << 16);
}

void amiga_fdc::dskptl_w(UINT16 data)
{
	live_sync();
	dskpt = (dskpt & 0xffff0000) | data;
}

UINT16 amiga_fdc::dskpth_r()
{
	return dskpt >> 16;
}

UINT16 amiga_fdc::dskptl_r()
{
	return dskpt;
}

void amiga_fdc::dsksync_w(UINT16 data)
{
	live_sync();
	dsksync = data;
}

void amiga_fdc::dmacon_set(UINT16 data)
{
	live_sync();
	dmacon = data;
	dma_check();
	dskbyt = dskbyt & 0xbfff;
	if(dma_state != DMA_IDLE)
		dskbyt |= 0x4000;
}

UINT16 amiga_fdc::dskbytr_r()
{
	return dskbyt;
}

void amiga_fdc::device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr)
{
	live_sync();
	live_run();
}

void amiga_fdc::setup_leds()
{
	if(floppy) {
		int drive =
			floppy == floppy_devices[0] ? 0 :
			floppy == floppy_devices[1] ? 1 :
			floppy == floppy_devices[2] ? 2 :
			3;

		output_set_value("drive_0_led", drive == 0);
		output_set_value("drive_1_led", drive == 1);
		output_set_value("drive_2_led", drive == 2);
		output_set_value("drive_3_led", drive == 3);

		set_led_status(machine(), 1, drive == 0); /* update internal drive led */
		set_led_status(machine(), 2, drive == 1); /* update external drive led */
	}
}

WRITE8_MEMBER( amiga_fdc::ciaaprb_w )
{
	live_sync();

	if(!(data & 0x08))
		floppy = floppy_devices[0];
	else if(!(data & 0x10))
		floppy = floppy_devices[1];
	else if(!(data & 0x20))
		floppy = floppy_devices[2];
	else if(!(data & 0x40))
		floppy = floppy_devices[3];
	else
		floppy = 0;

	if(floppy) {
		floppy->ss_w(!((data >> 2) & 1));
		floppy->dir_w((data >> 1) & 1);
		floppy->stp_w(data & 1);
		floppy->mon_w((data >> 7) & 1);
	}

	if(floppy) {
		if(cur_live.state == IDLE)
			live_start();
	} else
		live_abort();

	setup_leds();
}

UINT8 amiga_fdc::ciaapra_r()
{
	UINT8 ret = 0x3c;
	if(floppy) {
		// fixit
		ret &= ~0x20;

		if(!floppy->trk00_r())
			ret &= ~0x10;
		if(!floppy->wpt_r())
			ret &= ~0x08;
		if(!floppy->dskchg_r())
			ret &= ~0x04;
	}
	return ret;
}

void amiga_fdc::index_callback(floppy_image_device *floppy, int state)
{
	/* Issue a index pulse when a disk revolution completes */
	device_t *cia = machine().device("cia_1");
	mos6526_flag_w(cia, state);
}

void amiga_fdc::pll_t::set_clock(attotime period)
{
	for(int i=0; i<38; i++)
		delays[i] = period*(i+1);
}

void amiga_fdc::pll_t::reset(attotime when)
{
	counter = 0;
	increment = 146;
	transition_time = 0xffff;
	history = 0x80;
	slot = 0;
	ctime = when;
	phase_add = 0x00;
	phase_sub = 0x00;
	freq_add  = 0x00;
	freq_sub  = 0x00;
}

int amiga_fdc::pll_t::get_next_bit(attotime &tm, floppy_image_device *floppy, attotime limit)
{
	attotime when = floppy ? floppy->get_next_transition(ctime) : attotime::never;

	for(;;) {
		attotime etime = ctime+delays[slot];
		if(etime > limit)
			return -1;
		if(transition_time == 0xffff && !when.is_never() && etime >= when)
			transition_time = counter;
		if(slot < 8) {
			UINT8 mask = 1 << slot;
			if(phase_add & mask)
				counter += 258;
			else if(phase_sub & mask)
				counter += 34;
			else
				counter += increment;

			if((freq_add & mask) && increment < 159)
				increment++;
			else if((freq_sub & mask) && increment > 134)
				increment--;
		} else
			counter += increment;

		slot++;
		tm = etime;
		if(counter & 0x800)
			break;
	}

	int bit = transition_time != 0xffff;

	if(transition_time != 0xffff) {
		static const UINT8 pha[8] = { 0xf, 0x7, 0x3, 0x1, 0, 0, 0, 0 };
		static const UINT8 phs[8] = { 0, 0, 0, 0, 0x1, 0x3, 0x7, 0xf };
		static const UINT8 freqa[4][8] = {
			{ 0xf, 0x7, 0x3, 0x1, 0, 0, 0, 0 },
			{ 0x7, 0x3, 0x1, 0, 0, 0, 0, 0 },
			{ 0x7, 0x3, 0x1, 0, 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0, 0, 0 }
		};
		static const UINT8 freqs[4][8] = {
			{ 0, 0, 0, 0, 0, 0, 0, 0 },
			{ 0, 0, 0, 0, 0, 0x1, 0x3, 0x7 },
			{ 0, 0, 0, 0, 0, 0x1, 0x3, 0x7 },
			{ 0, 0, 0, 0, 0x1, 0x3, 0x7, 0xf },
		};

		int cslot = transition_time >> 8;
		phase_add = pha[cslot];
		phase_sub = phs[cslot];
		int way = transition_time & 0x400 ? 1 : 0;
		if(history & 0x80)
			history = way ? 0x80 : 0x83;
		else if(history & 0x40)
			history = way ? history & 2 : (history & 2) | 1;
		freq_add = freqa[history & 3][cslot];
		freq_sub = freqs[history & 3][cslot];
		history = way ? (history >> 1) | 2 : history >> 1;

	} else
		phase_add = phase_sub = freq_add = freq_sub = 0;

	counter &= 0x7ff;

	ctime = tm;
	transition_time = 0xffff;
	slot = 0;

	return bit;
}
