// license:BSD-3-Clause
// copyright-holders:Patrick Mackinlay

#ifndef MAME_BUS_INTERPRO_SR_SR_H
#define MAME_BUS_INTERPRO_SR_SR_H

#pragma once

#define MCFG_SR_OUT_IRQ0_CB(_devcb) \
	devcb = &sr_device::set_out_irq0_callback(*device, DEVCB_##_devcb);

#define MCFG_SR_OUT_IRQ1_CB(_devcb) \
	devcb = &sr_device::set_out_irq1_callback(*device, DEVCB_##_devcb);

#define MCFG_SR_OUT_IRQ2_CB(_devcb) \
	devcb = &sr_device::set_out_irq2_callback(*device, DEVCB_##_devcb);

#define MCFG_SR_SLOT_ADD(_srtag, _tag, _slot_intf, _def_slot, _fixed) \
	MCFG_DEVICE_ADD(_tag, SR_SLOT, 0) \
	MCFG_DEVICE_SLOT_INTERFACE(_slot_intf, _def_slot, _fixed) \
	sr_slot_device::static_set_sr_slot(*device, _srtag, _tag);

#define MCFG_SR_SLOT_REMOVE(_tag)    \
	MCFG_DEVICE_REMOVE(_tag)

class sr_slot_device : public device_t, public device_slot_interface
{
public:
	// construction/destruction
	sr_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// inline configuration
	static void static_set_sr_slot(device_t &device, const char *tag, const char *slottag);
protected:
	sr_slot_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);

	// device-level overrides
	virtual void device_start() override;

	// configuration
	const char *m_sr_tag, *m_sr_slottag;
};

class device_sr_card_interface;

class sr_device : public device_t
{
public:
	// construction/destruction
	sr_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// inline configuration
	template <class Object> static devcb_base &set_out_irq0_callback(device_t &device, Object &&cb) { return downcast<sr_device &>(device).m_out_irq0_cb.set_callback(std::forward<Object>(cb)); }
	template <class Object> static devcb_base &set_out_irq1_callback(device_t &device, Object &&cb) { return downcast<sr_device &>(device).m_out_irq1_cb.set_callback(std::forward<Object>(cb)); }
	template <class Object> static devcb_base &set_out_irq2_callback(device_t &device, Object &&cb) { return downcast<sr_device &>(device).m_out_irq2_cb.set_callback(std::forward<Object>(cb)); }

	DECLARE_WRITE_LINE_MEMBER(irq0_w) { m_out_irq0_cb(state); }
	DECLARE_WRITE_LINE_MEMBER(irq1_w) { m_out_irq1_cb(state); }
	DECLARE_WRITE_LINE_MEMBER(irq2_w) { m_out_irq2_cb(state); }

	// helper functions for card devices
	template <typename T> void install_card(T &device, void (T::*map)(address_map &map))
	{
		// record the device in the next free slot
		m_slot[m_slot_count] = &device;

		// compute slot base address
		offs_t start = 0x87000000 + m_slot_count * 0x8000000;
		offs_t end = start + 0x7ffffff;

		// install the device address map
		m_data_space->install_device(start, end, device, map, 32);
		m_io_space->install_device(start, end, device, map, 32);

		m_slot_count++;
	}

	void install_idprom(device_t *dev, const char *tag, const char *region);

protected:
	sr_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock);

	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;

	// internal state
	address_space *m_data_space;
	address_space *m_io_space;

	devcb_write_line m_out_irq0_cb;
	devcb_write_line m_out_irq1_cb;
	devcb_write_line m_out_irq2_cb;

private:
	device_sr_card_interface *m_slot[16];
	int m_slot_count;
};

class device_sr_card_interface : public device_slot_card_interface
{
public:
	// construction/destruction
	virtual ~device_sr_card_interface();

	virtual DECLARE_ADDRESS_MAP(map, 32) = 0;

	void set_sr_device();

	// inline configuration
	static void static_set_sr_tag(device_t &device, const char *tag, const char *slottag);

protected:
	device_sr_card_interface(const machine_config &mconfig, device_t &device);

	sr_device  *m_sr;
	const char *m_sr_tag, *m_sr_slottag;
};

class sr_card_device_base : public device_t, public device_sr_card_interface
{
protected:
	sr_card_device_base(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, const char *idprom_region = "idprom");

public:
	READ32_MEMBER(idprom_r) { return device().memregion(m_idprom_region)->as_u32(offset); }

protected:
	// device-level overrides
	virtual void device_start() override;

private:
	const char *const m_idprom_region;
};

DECLARE_DEVICE_TYPE(SR, sr_device)
DECLARE_DEVICE_TYPE(SR_SLOT, sr_slot_device)

#endif // MAME_BUS_INTERPRO_SR_SR_H
