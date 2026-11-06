/* sane - Scanner Access Now Easy.

   Copyright (C) 2019 Povilas Kanapickas <povilas@radix.lt>

   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#define DEBUG_DECLARE_ONLY

#include "usb_device.h"

namespace genesys {

IUsbDevice::~IUsbDevice() = default;

UsbDevice::~UsbDevice()
{
    if (is_open()) {
        DBG(DBG_error, "UsbDevice not closed; closing automatically");
        close();
    }
}

void UsbDevice::open(const char* dev_name)
{
    DBG_HELPER(dbg);

    if (is_open()) {
        throw SaneException("device already open");
    }
    int device_num = 0;

    dbg.status("open device");
    TIE(sanei_usb_open(dev_name, &device_num));

    name_ = dev_name;
    device_num_ = device_num;
    is_open_ = true;
}

void UsbDevice::clear_halt()
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_clear_halt(device_num_));
}

void UsbDevice::reset()
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_reset(device_num_));
}

void UsbDevice::close()
{
    DBG_HELPER(dbg);
    assert_is_open();

    // we can't do much if closing fails, so we close the device on our side regardless of the
    // function succeeds
    int device_num = device_num_;

    set_not_open();
    sanei_usb_close(device_num);
}

std::uint16_t UsbDevice::get_vendor_id()
{
    DBG_HELPER(dbg);
    assert_is_open();
    int vendor = 0;
    int product = 0;
    TIE(sanei_usb_get_vendor_product(device_num_, &vendor, &product));
    return static_cast<std::uint16_t>(vendor);
}

std::uint16_t UsbDevice::get_product_id()
{
    DBG_HELPER(dbg);
    assert_is_open();
    int vendor = 0;
    int product = 0;
    TIE(sanei_usb_get_vendor_product(device_num_, &vendor, &product));
    return static_cast<std::uint16_t>(product);
}

std::uint16_t UsbDevice::get_bcd_device()
{
    DBG_HELPER(dbg);
    assert_is_open();
    sanei_usb_dev_descriptor desc;
    TIE(sanei_usb_get_descriptor(device_num_, &desc));
    return desc.bcd_dev;
}

void UsbDevice::control_msg(int rtype, int reg, int value, int index, int length,
                            std::uint8_t* data)
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_control_msg(device_num_, rtype, reg, value, index, length, data));
}

void UsbDevice::bulk_read(std::uint8_t* buffer, std::size_t* size)
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_read_bulk(device_num_, buffer, size));
}

void UsbDevice::bulk_write(const std::uint8_t* buffer, std::size_t* size)
{
    DBG_HELPER(dbg);
    assert_is_open();
    TIE(sanei_usb_write_bulk(device_num_, buffer, size));
}

/* sanei_usb has one libusb timeout for bulk and interrupt reads alike and
   no getter for it, so the read sets the short timeout and puts back
   sanei_usb's default (30 s) afterwards, the convention gl646.cpp already
   follows. Nothing in this backend's GL126 path sets any other value. */
constexpr int kSaneiUsbDefaultTimeoutMs = 30 * 1000;

SANE_Status UsbDevice::interrupt_read(int endpoint, std::uint8_t* buffer, std::size_t* size,
                                      unsigned timeout_ms)
{
    assert_is_open();
    if (sanei_usb_get_endpoint(device_num_, USB_DIR_IN | USB_ENDPOINT_TYPE_INTERRUPT)
            != endpoint)
    {
        *size = 0;
        return SANE_STATUS_UNSUPPORTED;
    }
    sanei_usb_set_timeout(static_cast<SANE_Int>(timeout_ms));
    SANE_Status status = sanei_usb_read_int(device_num_, buffer, size);
    sanei_usb_set_timeout(kSaneiUsbDefaultTimeoutMs);
    if (status != SANE_STATUS_GOOD) {
        *size = 0;
    }
    return status;
}

void UsbDevice::assert_is_open() const
{
    if (!is_open()) {
        throw SaneException("device not open");
    }
}

void UsbDevice::set_not_open()
{
    device_num_ = 0;
    is_open_ = false;
    name_ = "";
}

} // namespace genesys
