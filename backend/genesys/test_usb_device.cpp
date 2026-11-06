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

#include "test_usb_device.h"
#include "low.h"
#include <cstdio>

namespace genesys {

namespace {

// " 0a 55 ..." for the transfer log.
std::string format_bytes(const std::uint8_t* data, std::size_t len)
{
    std::string out;
    char byte[4];
    for (std::size_t i = 0; i < len; ++i) {
        std::snprintf(byte, sizeof(byte), " %02x", data[i]);
        out += byte;
    }
    return out;
}

} // namespace

TestUsbDevice::TestUsbDevice(std::uint16_t vendor, std::uint16_t product,
                             std::uint16_t bcd_device) :
    vendor_{vendor},
    product_{product},
    bcd_device_{bcd_device}
{
}

TestUsbDevice::~TestUsbDevice()
{
    if (is_open()) {
        DBG(DBG_error, "TestUsbDevice not closed; closing automatically");
        close();
    }
}

void TestUsbDevice::open(const char* dev_name)
{
    DBG_HELPER(dbg);

    if (is_open()) {
        throw SaneException("device already open");
    }
    name_ = dev_name;
    is_open_ = true;
}

void TestUsbDevice::clear_halt()
{
    DBG_HELPER(dbg);
    assert_is_open();
}

void TestUsbDevice::reset()
{
    DBG_HELPER(dbg);
    assert_is_open();
}

void TestUsbDevice::close()
{
    DBG_HELPER(dbg);
    assert_is_open();

    is_open_ = false;
    name_ = "";
}

std::uint16_t TestUsbDevice::get_vendor_id()
{
    DBG_HELPER(dbg);
    assert_is_open();
    return vendor_;
}

std::uint16_t TestUsbDevice::get_product_id()
{
    DBG_HELPER(dbg);
    assert_is_open();
    return product_;
}

std::uint16_t TestUsbDevice::get_bcd_device()
{
    DBG_HELPER(dbg);
    assert_is_open();
    return bcd_device_;
}

void TestUsbDevice::control_msg(int rtype, int reg, int value, int index, int length,
                                std::uint8_t* data)
{
    DBG_HELPER(dbg);
    assert_is_open();
    if (rtype == REQUEST_TYPE_IN) {
        std::memset(data, 0, length);
        if (control_in_reply_) {
            control_in_reply_(reg, value, index, length, data);
        }
    } else {
        ++out_transfer_count_;
        if (observer_) {
            observer_(Transfer::ControlOut, reg, value, index,
                      static_cast<std::size_t>(length), data);
        }
    }
    if (record_transfers_) {
        char head[64];
        std::snprintf(head, sizeof(head), "CTRL %s req=0x%02x val=0x%04x idx=0x%04x len=%d:",
                      rtype == REQUEST_TYPE_IN ? "IN" : "OUT", reg, value, index, length);
        record_transfer(head + format_bytes(data, static_cast<std::size_t>(length)));
    }
}

void TestUsbDevice::bulk_read(std::uint8_t* buffer, std::size_t* size)
{

    DBG_HELPER(dbg);
    assert_is_open();
    std::memset(buffer, 0, *size);
    if (observer_) {
        observer_(Transfer::BulkIn, 0, 0, 0, *size, buffer);
    }
    if (record_transfers_) {
        record_transfer("BULK IN len=" + std::to_string(*size) + ":" +
                        format_bytes(buffer, *size));
    }
}

void TestUsbDevice::bulk_write(const std::uint8_t* buffer, std::size_t* size)
{
    DBG_HELPER(dbg);
    assert_is_open();
    ++out_transfer_count_;
    if (observer_) {
        observer_(Transfer::BulkOut, 0, 0, 0, *size, buffer);
    }
    if (record_transfers_) {
        record_transfer("BULK OUT len=" + std::to_string(*size) + ":" +
                        format_bytes(buffer, *size));
    }
}

void TestUsbDevice::set_record_transfers(bool record)
{
    record_transfers_ = record;
    transfer_log_.clear();
    repeat_count_ = 0;
    held_ = false;
}

void TestUsbDevice::record_transfer(const std::string& line)
{
    if (!record_transfers_) {
        return;
    }
    std::size_t n = transfer_log_.size();
    if (held_) {
        held_ = false;
        if (line == transfer_log_[n - 1]) {
            ++repeat_count_;    // the pair came once more
            return;
        }
        // Not a repetition after all: end the count, store the held line
        // and go on with this one.
        if (repeat_count_ != 0) {
            transfer_log_.push_back("REPEAT 2 x" + std::to_string(repeat_count_));
            repeat_count_ = 0;
        }
        transfer_log_.push_back(held_line_);
        n = transfer_log_.size();
    }
    if (n >= 2 && line == transfer_log_[n - 2] && line != transfer_log_[n - 1]) {
        held_ = true;
        held_line_ = line;
        return;
    }
    if (repeat_count_ != 0) {
        transfer_log_.push_back("REPEAT 2 x" + std::to_string(repeat_count_));
        repeat_count_ = 0;
    }
    transfer_log_.push_back(line);
}

std::vector<std::string> TestUsbDevice::transfer_log() const
{
    std::vector<std::string> log = transfer_log_;
    if (repeat_count_ != 0) {
        log.push_back("REPEAT 2 x" + std::to_string(repeat_count_));
    }
    if (held_) {
        log.push_back(held_line_);
    }
    return log;
}

void TestUsbDevice::assert_is_open() const
{
    if (!is_open()) {
        throw SaneException("device not open");
    }
}

} // namespace genesys
