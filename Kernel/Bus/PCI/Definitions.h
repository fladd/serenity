/*
 * Copyright (c) 2020, Liav A. <liavalb@hotmail.co.il>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/String.h>
#include <AK/Types.h>
#include <AK/Vector.h>
#include <Kernel/Debug.h>

namespace Kernel {

#define PCI_VENDOR_ID 0x00            // word
#define PCI_DEVICE_ID 0x02            // word
#define PCI_COMMAND 0x04              // word
#define PCI_STATUS 0x06               // word
#define PCI_REVISION_ID 0x08          // byte
#define PCI_PROG_IF 0x09              // byte
#define PCI_SUBCLASS 0x0a             // byte
#define PCI_CLASS 0x0b                // byte
#define PCI_CACHE_LINE_SIZE 0x0c      // byte
#define PCI_LATENCY_TIMER 0x0d        // byte
#define PCI_HEADER_TYPE 0x0e          // byte
#define PCI_BIST 0x0f                 // byte
#define PCI_BAR0 0x10                 // u32
#define PCI_BAR1 0x14                 // u32
#define PCI_BAR2 0x18                 // u32
#define PCI_BAR3 0x1C                 // u32
#define PCI_BAR4 0x20                 // u32
#define PCI_BAR5 0x24                 // u32
#define PCI_SUBSYSTEM_VENDOR_ID 0x2C  // u16
#define PCI_SUBSYSTEM_ID 0x2E         // u16
#define PCI_CAPABILITIES_POINTER 0x34 // u8
#define PCI_INTERRUPT_LINE 0x3C       // byte
#define PCI_SECONDARY_BUS 0x19        // byte
#define PCI_HEADER_TYPE_DEVICE 0
#define PCI_HEADER_TYPE_BRIDGE 1
#define PCI_TYPE_BRIDGE 0x0604
#define PCI_ADDRESS_PORT 0xCF8
#define PCI_VALUE_PORT 0xCFC
#define PCI_NONE 0xFFFF
#define PCI_MAX_DEVICES_PER_BUS 32
#define PCI_MAX_BUSES 256
#define PCI_MAX_FUNCTIONS_PER_DEVICE 8

#define PCI_CAPABILITY_NULL 0x0
#define PCI_CAPABILITY_MSI 0x5
#define PCI_CAPABILITY_VENDOR_SPECIFIC 0x9
#define PCI_CAPABILITY_MSIX 0x11

// Taken from https://pcisig.com/sites/default/files/files/PCI_Code-ID_r_1_11__v24_Jan_2019.pdf
#define PCI_MASS_STORAGE_CLASS_ID 0x1
#define PCI_IDE_CTRL_SUBCLASS_ID 0x1
#define PCI_SATA_CTRL_SUBCLASS_ID 0x6
#define PCI_AHCI_IF_PROGIF 0x1

namespace PCI {
struct ID {
    u16 vendor_id { 0 };
    u16 device_id { 0 };

    bool is_null() const { return !vendor_id && !device_id; }

    bool operator==(const ID& other) const
    {
        return vendor_id == other.vendor_id && device_id == other.device_id;
    }
    bool operator!=(const ID& other) const
    {
        return vendor_id != other.vendor_id || device_id != other.device_id;
    }
};

class Domain {
public:
    Domain() = delete;
    Domain(PhysicalAddress base_address, u8 start_bus, u8 end_bus)
        : m_base_addr(base_address)
        , m_start_bus(start_bus)
        , m_end_bus(end_bus)
    {
    }
    u8 start_bus() const { return m_start_bus; }
    u8 end_bus() const { return m_end_bus; }
    PhysicalAddress paddr() const { return m_base_addr; }

private:
    PhysicalAddress m_base_addr;
    u8 m_start_bus;
    u8 m_end_bus;
};

struct Address {
public:
    Address() = default;
    Address(u32 domain)
        : m_domain(domain)
        , m_bus(0)
        , m_device(0)
        , m_function(0)
    {
    }
    Address(u32 domain, u8 bus, u8 device, u8 function)
        : m_domain(domain)
        , m_bus(bus)
        , m_device(device)
        , m_function(function)
    {
    }

    Address(const Address& address)
        : m_domain(address.domain())
        , m_bus(address.bus())
        , m_device(address.device())
        , m_function(address.function())
    {
    }

    bool is_null() const { return !m_bus && !m_device && !m_function; }
    operator bool() const { return !is_null(); }

    // Disable default implementations that would use surprising integer promotion.
    bool operator<=(const Address&) const = delete;
    bool operator>=(const Address&) const = delete;
    bool operator<(const Address&) const = delete;
    bool operator>(const Address&) const = delete;

    bool operator==(const Address& other) const
    {
        if (this == &other)
            return true;
        return m_domain == other.m_domain && m_bus == other.m_bus && m_device == other.m_device && m_function == other.m_function;
    }
    bool operator!=(const Address& other) const
    {
        return !(*this == other);
    }

    u16 domain() const { return m_domain; }
    u8 bus() const { return m_bus; }
    u8 device() const { return m_device; }
    u8 function() const { return m_function; }

    u32 io_address_for_field(u8 field) const
    {
        return 0x80000000u | (m_bus << 16u) | (m_device << 11u) | (m_function << 8u) | (field & 0xfc);
    }

private:
    u32 m_domain { 0 };
    u8 m_bus { 0 };
    u8 m_device { 0 };
    u8 m_function { 0 };
};

class Capability {
public:
    Capability(const Address& address, u8 id, u8 ptr)
        : m_address(address)
        , m_id(id)
        , m_ptr(ptr)
    {
    }

    u8 id() const { return m_id; }

    u8 read8(u32) const;
    u16 read16(u32) const;
    u32 read32(u32) const;
    void write8(u32, u8);
    void write16(u32, u16);
    void write32(u32, u32);

private:
    Address m_address;
    const u8 m_id;
    const u8 m_ptr;
};

class PhysicalID {
public:
    PhysicalID(Address address, ID id, Vector<Capability> capabilities)
        : m_address(address)
        , m_id(id)
        , m_capabilities(capabilities)
    {
        if constexpr (PCI_DEBUG) {
            for (const auto& capability : capabilities)
                dbgln("{} has capability {}", address, capability.id());
        }
    }

    Vector<Capability> capabilities() const { return m_capabilities; }
    const ID& id() const { return m_id; }
    const Address& address() const { return m_address; }

private:
    Address m_address;
    ID m_id;
    Vector<Capability> m_capabilities;
};

class Access;
class Domain;
class Device;
}

}

template<>
struct AK::Formatter<Kernel::PCI::Address> : Formatter<FormatString> {
    void format(FormatBuilder& builder, Kernel::PCI::Address value)
    {
        return Formatter<FormatString>::format(
            builder,
            "PCI [{:04x}:{:02x}:{:02x}:{:02x}]", value.domain(), value.bus(), value.device(), value.function());
    }
};

template<>
struct AK::Formatter<Kernel::PCI::ID> : Formatter<FormatString> {
    void format(FormatBuilder& builder, Kernel::PCI::ID value)
    {
        return Formatter<FormatString>::format(
            builder,
            "PCI::ID [{:04x}:{:04x}]", value.vendor_id, value.device_id);
    }
};
