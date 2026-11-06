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

#ifndef BACKEND_GENESYS_TEST_USB_DEVICE_H
#define BACKEND_GENESYS_TEST_USB_DEVICE_H

#include "usb_device.h"
#include <functional>
#include <string>
#include <vector>

namespace genesys {

class TestUsbDevice : public IUsbDevice {
public:
    TestUsbDevice(std::uint16_t vendor, std::uint16_t product, std::uint16_t bcd_device);
    ~TestUsbDevice() override;

    bool is_open() const override { return is_open_; }

    const std::string& name() const override { return name_; }

    void open(const char* dev_name) override;

    void clear_halt() override;
    void reset() override;
    void close() override;

    std::uint16_t get_vendor_id() override;
    std::uint16_t get_product_id() override;
    std::uint16_t get_bcd_device() override;

    void control_msg(int rtype, int reg, int value, int index, int length,
                     std::uint8_t* data) override;
    void bulk_read(std::uint8_t* buffer, std::size_t* size) override;
    void bulk_write(const std::uint8_t* buffer, std::size_t* size) override;
    SANE_Status interrupt_read(int endpoint, std::uint8_t* buffer, std::size_t* size,
                               unsigned timeout_ms) override;

    /* The interrupt endpoint's script, off unless a test sets it. Added
       for the GL126 magazine load's interrupt drain: each interrupt read
       answers the next entry, either a 1-byte event (0..255) or, as
       -status, a sanei status (-SANE_STATUS_IO_ERROR is what sanei_usb
       returns for a timeout). Once the script is exhausted, or with no
       script, every read answers SANE_STATUS_IO_ERROR with no data: an
       idle endpoint. Like the IN control transfers these reads are not
       OUT transfers, so out_transfer_count() does not count them; the
       transfer log records each one ("INT IN ep=0x83 len=1: 48" or
       "INT IN ep=0x83 len=1: status 9"). Setting a script replaces the
       previous one. */
    void set_interrupt_script(std::vector<int> script);

    /* How many OUT transfers (a control_msg with rtype != REQUEST_TYPE_IN,
       or a bulk_write) this mock has answered since the last reset,
       "answered", not "acknowledged": the mock never refuses a write, it
       just counts it. Added for the GL126 magazine edge wait: that wait
       is a read-only loop by construction (it only ever calls
       read_register / sleep_ms), but a regression test wants to assert
       that on the wire too, from a checkpoint callback that snapshots
       this count at the first poll and checks it is unchanged at every
       later one. Every other genesys test ignores this counter; it
       costs them nothing. */
    unsigned out_transfer_count() const { return out_transfer_count_; }
    void reset_out_transfer_count() { out_transfer_count_ = 0; }

    /* Two more test-only aids, both off unless a test sets them, so every
       other genesys test sees the mock exactly as before. Added for the
       GL126 magazine load: its op programs check what the device answers
       (an ack read must return 0x55, a motor completion poll a masked
       status class), and a mock that only ever answers zeroes stops every
       program at its first ack read, long before the polls a test needs
       to reach.

       The reply hook is called for every IN control transfer, after the
       zero fill, and may overwrite the reply; with no hook the reply
       stays all zeroes. */
    using ControlInReply = std::function<void(int reg, int value, int index, int length,
                                              std::uint8_t* data)>;
    void set_control_in_reply(ControlInReply reply) { control_in_reply_ = std::move(reply); }

    /* The transfer log: one line per transfer, in order, with its
       setup fields and every byte sent or answered, so a test can assert
       that two builds put the identical sequence on the wire, not just
       the same number of transfers. TestScannerInterface also records
       here (record_transfer()) its register and buffer methods, which
       never reach this mock but are USB traffic on hardware, and its
       sleeps, so a stray register read or a changed wait shows up too.

       A poll loop (a read, a sleep, the same read, the same sleep, ...)
       is stored once, followed by "REPEAT 2 x<n>": the two lines above
       it came n more times. A test-mode poll that never matches spins
       for its whole real-time budget, millions of iterations, which
       could not be stored line by line, and how many it gets in that
       time depends on the host. Starting a recording clears the log. */
    void set_record_transfers(bool record);
    bool records_transfers() const { return record_transfers_; }
    void record_transfer(const std::string& line);
    std::vector<std::string> transfer_log() const;

private:
    void assert_is_open() const;

    std::string name_;
    bool is_open_ = false;
    std::uint16_t vendor_ = 0;
    std::uint16_t product_ = 0;
    std::uint16_t bcd_device_ = 0;
    unsigned out_transfer_count_ = 0;
    ControlInReply control_in_reply_;
    bool record_transfers_ = false;
    std::vector<std::string> transfer_log_;
    // The pair repetition being counted (see transfer_log()): how many
    // more times the last two stored lines came, and the first line of
    // a possible next repetition, held until the second one arrives.
    unsigned repeat_count_ = 0;
    bool held_ = false;
    std::string held_line_;
    std::vector<int> interrupt_script_;
    std::size_t interrupt_step_ = 0;
};

} // namespace genesys

#endif // BACKEND_GENESYS_TEST_USB_DEVICE_H
