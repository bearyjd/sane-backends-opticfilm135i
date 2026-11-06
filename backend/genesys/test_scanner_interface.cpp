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

#include "test_scanner_interface.h"
#include "device.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace genesys {

namespace {

// Records one transfer-log line (TestUsbDevice::record_transfer()) for
// a method of this class that is USB traffic on hardware (a register
// write may share one transfer with others there, it is logged one line
// per register here) or a sleep, formatted only while a recording is on.
void record_transfer(TestUsbDevice& usb, const char* format, ...)
    __attribute__((format(printf, 2, 3)));

void record_transfer(TestUsbDevice& usb, const char* format, ...)
{
    if (!usb.records_transfers()) {
        return;
    }
    char line[64];
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    usb.record_transfer(line);
}

// The same, followed by the `size` bytes written.
void record_transfer_bytes(TestUsbDevice& usb, const std::uint8_t* data, std::size_t size,
                           const char* format, ...) __attribute__((format(printf, 4, 5)));

void record_transfer_bytes(TestUsbDevice& usb, const std::uint8_t* data, std::size_t size,
                           const char* format, ...)
{
    if (!usb.records_transfers()) {
        return;
    }
    char line[64];
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    std::string out = line;
    char byte[4];
    for (std::size_t i = 0; i < size; ++i) {
        std::snprintf(byte, sizeof(byte), " %02x", data[i]);
        out += byte;
    }
    usb.record_transfer(out);
}

} // namespace

TestScannerInterface::TestScannerInterface(Genesys_Device* dev, std::uint16_t vendor_id,
                                           std::uint16_t product_id, std::uint16_t bcd_device) :
    dev_{dev},
    usb_dev_{vendor_id, product_id, bcd_device}
{
    // initialize status registers
    if (dev_->model->asic_type == AsicType::GL124) {
        write_register(0x101, 0x00);
    } else if (dev_->model->asic_type == AsicType::GL126) {
        // GL126 (OpticFilm 135i): its status address is 0x101 like GL124's,
        // and its init()/offset_calibration read reg 0x01 directly (the
        // idle-homed check). Seed both so the backend can be driven in
        // test mode; 0x01 = 0x00 reads as "cold, never homed", which
        // makes offset_calibration stop early (before the lamp-warmup
        // loop), enough to observe that calibration was entered.
        write_register(0x01, 0x00);
        write_register(0x101, 0x00);
        // Regs 0x3b/0x3c: the magazine flow reads these unconditionally,
        // the Load film / Check status hooks as preconditions/
        // diagnostics, "eject" as a refusal check, and
        // RegisterCache::get() throws for an address that was never
        // written, unlike real hardware, which always answers a
        // register read with something. Seeded to 0x00/0x00 (the state
        // a device-open table leaves) so test-mode scenarios that only
        // care about reg 0x01/0x101 do not have to seed these too just
        // to avoid that mock-only throw.
        write_register(0x3B, 0x00);
        write_register(0x3C, 0x00);
    } else {
        write_register(0x41, 0x00);
    }
    if (dev_->model->asic_type == AsicType::GL841 ||
        dev_->model->asic_type == AsicType::GL842 ||
        dev_->model->asic_type == AsicType::GL843 ||
        dev_->model->asic_type == AsicType::GL845 ||
        dev_->model->asic_type == AsicType::GL846 ||
        dev_->model->asic_type == AsicType::GL847)
    {
        write_register(0x40, 0x00);
    }

    // initialize other registers that we read on init
    if (dev_->model->asic_type == AsicType::GL124) {
        write_register(0x33, 0x00);
        write_register(0xbd, 0x00);
        write_register(0xbe, 0x00);
        write_register(0x100, 0x00);
    }

    if (dev_->model->asic_type == AsicType::GL845 ||
        dev_->model->asic_type == AsicType::GL846 ||
        dev_->model->asic_type == AsicType::GL847)
    {
        write_register(0xbd, 0x00);
        write_register(0xbe, 0x00);

        write_register(0xd0, 0x00);
        write_register(0xd1, 0x01);
        write_register(0xd2, 0x02);
        write_register(0xd3, 0x03);
        write_register(0xd4, 0x04);
        write_register(0xd5, 0x05);
        write_register(0xd6, 0x06);
        write_register(0xd7, 0x07);
        write_register(0xd8, 0x08);
        write_register(0xd9, 0x09);
    }
}

TestScannerInterface::~TestScannerInterface() = default;

bool TestScannerInterface::is_mock() const
{
    return true;
}

std::uint8_t TestScannerInterface::read_register(std::uint16_t address)
{
    std::uint8_t value = cached_regs_.get(address);
    record_transfer(usb_dev_, "REG R 0x%04x: %02x", address, value);
    return value;
}

void TestScannerInterface::write_register(std::uint16_t address, std::uint8_t value)
{
    record_transfer(usb_dev_, "REG W 0x%04x: %02x", address, value);
    cached_regs_.update(address, value);
    ++cache_write_count_;
}

void TestScannerInterface::write_registers(const Genesys_Register_Set& regs)
{
    for (const auto& reg : regs) {
        record_transfer(usb_dev_, "REG W 0x%04x: %02x", reg.address, reg.value);
    }
    cached_regs_.update(regs);
    ++cache_write_count_;
}


void TestScannerInterface::write_0x8c(std::uint8_t index, std::uint8_t value)
{
    record_transfer(usb_dev_, "0x8C W 0x%02x: %02x", index, value);
    ++cache_write_count_;
}

void TestScannerInterface::bulk_read_data(std::uint8_t addr, std::uint8_t* data, std::size_t size)
{
    std::memset(data, 0, size);
    record_transfer(usb_dev_, "DATA R 0x%02x len=%zu", addr, size);
}

void TestScannerInterface::bulk_write_data(std::uint8_t addr, std::uint8_t* data, std::size_t size)
{
    record_transfer_bytes(usb_dev_, data, size, "DATA W 0x%02x len=%zu:", addr, size);
    ++cache_write_count_;
}

void TestScannerInterface::write_buffer(std::uint8_t type, std::uint32_t addr, std::uint8_t* data,
                                        std::size_t size)
{
    record_transfer_bytes(usb_dev_, data, size, "BUFFER W 0x%02x 0x%08x len=%zu:", type,
                          addr, size);
    ++cache_write_count_;
}

void TestScannerInterface::write_gamma(std::uint8_t type, std::uint32_t addr, std::uint8_t* data,
                                       std::size_t size)
{
    record_transfer_bytes(usb_dev_, data, size, "GAMMA W 0x%02x 0x%08x len=%zu:", type,
                          addr, size);
    ++cache_write_count_;
}

void TestScannerInterface::write_ahb(std::uint32_t addr, std::uint32_t size, std::uint8_t* data)
{
    record_transfer_bytes(usb_dev_, data, size, "AHB W 0x%08x len=%u:", addr, size);
    ++cache_write_count_;
}

std::uint16_t TestScannerInterface::read_fe_register(std::uint8_t address)
{
    std::uint16_t value = cached_fe_regs_.get(address);
    record_transfer(usb_dev_, "FE R 0x%02x: %04x", address, value);
    return value;
}

void TestScannerInterface::write_fe_register(std::uint8_t address, std::uint16_t value)
{
    record_transfer(usb_dev_, "FE W 0x%02x: %04x", address, value);
    cached_fe_regs_.update(address, value);
    ++cache_write_count_;
}

IUsbDevice& TestScannerInterface::get_usb_device()
{
    return usb_dev_;
}

void TestScannerInterface::sleep_us(unsigned microseconds)
{
    record_transfer(usb_dev_, "SLEEP %u us", microseconds);
}

void TestScannerInterface::record_slope_table(unsigned table_nr,
                                              const std::vector<std::uint16_t>& steps)
{
    slope_tables_[table_nr] = steps;
}

std::map<unsigned, std::vector<std::uint16_t>>& TestScannerInterface::recorded_slope_tables()
{
    return slope_tables_;
}

void TestScannerInterface::record_progress_message(const char* msg)
{
    last_progress_message_ = msg;
}

const std::string& TestScannerInterface::last_progress_message() const
{
    return last_progress_message_;
}

void TestScannerInterface::record_key_value(const std::string& key, const std::string& value)
{
    key_values_[key] = value;
}

std::map<std::string, std::string>& TestScannerInterface::recorded_key_values()
{
    return key_values_;
}

void TestScannerInterface::test_checkpoint(const std::string& name)
{
    if (checkpoint_callback_) {
        checkpoint_callback_(*dev_, *this, name);
    }
}

TestUsbDevice& TestScannerInterface::test_usb_device()
{
    return usb_dev_;
}

void TestScannerInterface::set_checkpoint_callback(TestCheckpointCallback callback)
{
    checkpoint_callback_ = callback;
}

} // namespace genesys
