#include "arch/x86_64/apic.h"

extern "C" {
    #include "arch/x86_64/io.h"
}

#include "arch/x86_64/paging64.h"
#include "arch/x86_64/idt64.h"
#include "kernel/acpi.h"
#include "kernel/klog.h"
#include "kernel/kutil64.h"

#define IA32_APIC_BASE_MSR 0x1Bu
#define IA32_APIC_BASE_ENABLE (1ULL << 11)
#define LAPIC_ID 0x020u
#define LAPIC_EOI 0x0B0u
#define LAPIC_SPURIOUS 0x0F0u
#define LAPIC_SOFTWARE_ENABLE 0x100u

static uint32_t controller_mode = INTERRUPT_CONTROLLER_PIC;
static volatile uint32_t* lapic = 0;
static volatile uint32_t* ioapic = 0;
static uint32_t ioapic_gsi_base = 0;
static uint32_t ioapic_redirections = 0;
static uint32_t irq_gsi[16];

static uint64_t read_msr(uint32_t msr) {
    uint32_t low = 0;
    uint32_t high = 0;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : :
                     "c"(msr),
                     "a"((uint32_t)value),
                     "d"((uint32_t)(value >> 32)));
}

static int cpu_has_apic() {
    uint32_t eax = 1;
    uint32_t edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=d"(edx) : : "rbx", "rcx");
    return (edx & (1u << 9)) != 0;
}

static uint32_t lapic_read(uint32_t offset) {
    return lapic[offset / sizeof(uint32_t)];
}

static void lapic_write(uint32_t offset, uint32_t value) {
    lapic[offset / sizeof(uint32_t)] = value;
    (void)lapic_read(LAPIC_ID);
}

static uint32_t ioapic_read(uint8_t reg) {
    ioapic[0] = reg;
    return ioapic[4];
}

static void ioapic_write(uint8_t reg, uint32_t value) {
    ioapic[0] = reg;
    ioapic[4] = value;
}

static void ioapic_mask_all() {
    for (uint32_t index = 0; index < ioapic_redirections; index++) {
        ioapic_write((uint8_t)(0x10 + index * 2), 1u << 16);
        ioapic_write((uint8_t)(0x11 + index * 2), 0);
    }
}

static int ioapic_route(uint8_t irq, uint8_t vector, const AcpiState* acpi) {
    uint32_t gsi = irq;
    uint16_t flags = 0;
    const AcpiInterruptOverride* iso = acpi_find_override(irq);
    if (iso != 0) {
        gsi = iso->gsi;
        flags = iso->flags;
    }
    if (gsi < ioapic_gsi_base ||
        gsi - ioapic_gsi_base >= ioapic_redirections) {
        return 0;
    }

    uint32_t low = vector;
    uint16_t polarity = flags & 0x3u;
    uint16_t trigger = (flags >> 2) & 0x3u;
    if (polarity == 3u) {
        low |= 1u << 13;
    }
    if (trigger == 3u) {
        low |= 1u << 15;
    }

    uint32_t destination = lapic_read(LAPIC_ID) & 0xFF000000u;
    uint32_t index = gsi - ioapic_gsi_base;
    ioapic_write((uint8_t)(0x11 + index * 2), destination);
    ioapic_write((uint8_t)(0x10 + index * 2), low);
    if (irq < 16) {
        irq_gsi[irq] = gsi;
    }
    return 1;
}

static void disable_legacy_pic() {
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

static void activate_legacy_pic() {
    if (controller_mode == INTERRUPT_CONTROLLER_APIC && ioapic != 0) {
        ioapic_mask_all();
        if (lapic != 0) {
            lapic_write(LAPIC_EOI, 0);
        }
    }

    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
    controller_mode = INTERRUPT_CONTROLLER_PIC;
    lapic = 0;
    ioapic = 0;
    ioapic_gsi_base = 0;
    ioapic_redirections = 0;
    for (uint32_t i = 0; i < 16; i++) {
        irq_gsi[i] = 0xFFFFFFFFu;
    }
}

int interrupt_controller_init(const AcpiState* acpi) {
    if (acpi == 0 || !acpi->ready || acpi->ioapic_count == 0 ||
        !cpu_has_apic()) {
        activate_legacy_pic();
        klog_write(KLOG_WARN, "interrupt", "using legacy PIC");
        return 0;
    }

    lapic = (volatile uint32_t*)(uintptr_t)acpi->local_apic_address;
    ioapic = (volatile uint32_t*)(uintptr_t)acpi->ioapics[0].address;
    ioapic_gsi_base = acpi->ioapics[0].gsi_base;
    if (lapic == 0 || ioapic == 0) {
        return 0;
    }

    uint64_t mmio_flags = PAGING64_FLAG_WRITABLE |
                          PAGING64_FLAG_CACHE_DISABLE |
                          PAGING64_FLAG_NX;
    if (!paging64_map_range_identity(acpi->local_apic_address, 4096, mmio_flags) ||
        !paging64_map_range_identity(acpi->ioapics[0].address, 4096, mmio_flags)) {
        klog_write(KLOG_ERROR, "interrupt", "failed to map APIC MMIO");
        activate_legacy_pic();
        return 0;
    }

    uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);
    apic_base |= IA32_APIC_BASE_ENABLE;
    write_msr(IA32_APIC_BASE_MSR, apic_base);
    lapic_write(LAPIC_SPURIOUS,
                (lapic_read(LAPIC_SPURIOUS) & 0xFFFFFF00u) |
                LAPIC_SOFTWARE_ENABLE |
                0xFFu);

    uint32_t version = ioapic_read(1);
    ioapic_redirections = ((version >> 16) & 0xFFu) + 1u;
    ioapic_mask_all();
    if (!ioapic_route(0, 32, acpi) || !ioapic_route(1, 33, acpi)) {
        klog_write(KLOG_ERROR, "interrupt", "IOAPIC routing failed; using PIC");
        activate_legacy_pic();
        return 0;
    }

    disable_legacy_pic();
    controller_mode = INTERRUPT_CONTROLLER_APIC;
    klog_write(KLOG_INFO, "interrupt", "Local APIC and IOAPIC enabled");
    return 1;
}

void interrupt_controller_eoi(uint8_t irq) {
    if (controller_mode == INTERRUPT_CONTROLLER_APIC && lapic != 0) {
        lapic_write(LAPIC_EOI, 0);
        return;
    }
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

void interrupt_controller_set_mask(uint8_t irq, int masked) {
    if (controller_mode == INTERRUPT_CONTROLLER_APIC && ioapic != 0 &&
        irq < 16 && irq_gsi[irq] != 0xFFFFFFFFu) {
        uint32_t index = irq_gsi[irq] - ioapic_gsi_base;
        uint8_t reg = (uint8_t)(0x10 + index * 2);
        uint32_t low = ioapic_read(reg);
        if (masked) {
            low |= 1u << 16;
        } else {
            low &= ~(1u << 16);
        }
        ioapic_write(reg, low);
        return;
    }

    uint16_t port = irq < 8 ? 0x21 : 0xA1;
    uint8_t bit = irq < 8 ? irq : (uint8_t)(irq - 8);
    uint8_t mask = inb(port);
    if (masked) {
        mask |= (uint8_t)(1u << bit);
    } else {
        mask &= (uint8_t)~(1u << bit);
    }
    outb(port, mask);
}

int interrupt_controller_irq_masked(uint8_t irq) {
    if (controller_mode == INTERRUPT_CONTROLLER_APIC && ioapic != 0 &&
        irq < 16 && irq_gsi[irq] != 0xFFFFFFFFu) {
        uint32_t index = irq_gsi[irq] - ioapic_gsi_base;
        return (ioapic_read((uint8_t)(0x10 + index * 2)) & (1u << 16)) != 0;
    }
    uint16_t port = irq < 8 ? 0x21 : 0xA1;
    uint8_t bit = irq < 8 ? irq : (uint8_t)(irq - 8);
    return (inb(port) & (1u << bit)) != 0;
}

uint32_t interrupt_controller_mode() {
    return controller_mode;
}

const char* interrupt_controller_name() {
    return controller_mode == INTERRUPT_CONTROLLER_APIC ? "APIC" : "PIC";
}

void interrupt_controller_print() {
    print("\n=== INTERRUPT CONTROLLER ===");
    print("\nmode=");
    print(interrupt_controller_name());
    print(" lapic=");
    print_hex64((uint64_t)(uintptr_t)lapic);
    print(" ioapic=");
    print_hex64((uint64_t)(uintptr_t)ioapic);
    print(" redirections=");
    print_hex32(ioapic_redirections);
    print("\npic_spurious_count=");
    print_hex32(pic_spurious_irq7_count() + pic_spurious_irq15_count());
    print(" irq7=");
    print_hex32(pic_spurious_irq7_count());
    print(" irq15=");
    print_hex32(pic_spurious_irq15_count());
    if (controller_mode == INTERRUPT_CONTROLLER_APIC) {
        print("\nlapic_isr32=");
        print_hex32(lapic_read(0x110));
        print(" lapic_irr32=");
        print_hex32(lapic_read(0x210));
        for (uint32_t irq = 0; irq < 2; irq++) {
            if (irq_gsi[irq] == 0xFFFFFFFFu) {
                continue;
            }
            uint32_t index = irq_gsi[irq] - ioapic_gsi_base;
            print("\nirq=");
            print_hex32(irq);
            print(" gsi=");
            print_hex32(irq_gsi[irq]);
            print(" low=");
            print_hex32(ioapic_read((uint8_t)(0x10 + index * 2)));
            print(" high=");
            print_hex32(ioapic_read((uint8_t)(0x11 + index * 2)));
        }
    }
    print("\n============================\n");
}
