#include <printk.h>
#include <system.h>
#include <machine/acpi.h>
#include <x86_64-madt.h>
#include <mmu.h>
#include <x86_64-ioport.h>
#include <panic.h>
#define MAX_IOAPICS 8

struct ioapic {
	int id;
	uintptr_t vaddr;
	int gsib;
};

struct ioapic ioapics[MAX_IOAPICS];

static void write_ioapic(struct ioapic *chip, const uint8_t offset, const uint32_t val)
{
 	/* tell IOREGSEL where we want to write to */
 	*(volatile uint32_t*)(chip->vaddr) = offset;
 	/* write the value to IOWIN */
 	*(volatile uint32_t*)(chip->vaddr + 0x10) = val;
}

#if 0
static uint32_t read_ioapic(struct ioapic *chip, const uint8_t offset)
{
 	/* tell IOREGSEL where we want to read from */
 	*(volatile uint32_t*)(chip->vaddr) = offset;
 	/* return the data from IOWIN */
 	return *(volatile uint32_t*)(chip->vaddr + 0x10);
}
#endif 

void ioapic_init(struct ioapic_entry *entry)
{
	if(entry->apicid >= MAX_IOAPICS)
		return;
	ioapics[entry->apicid].id = entry->apicid;
	ioapics[entry->apicid].vaddr = entry->ioapicaddr + PHYS_MAP_START;
	ioapics[entry->apicid].gsib = entry->gsib;
}

static void write_ioapic_vector(struct ioapic *l, int irq, char masked,
		char trigger, char polarity, char mode, int vector)
{
	uint32_t lower=0, higher=0;
	lower = vector & 0xFF;
	/* 8-10: delivery mode */
	lower |= (mode << 8) & 0x700;
	/* 13: polarity */
	if(polarity) lower |= (1 << 13);
	/* 15: trigger */
	if(trigger) lower |= (1 << 15);
	/* 16: mask */
	if(masked) lower |= (1 << 16);
	/* 56-63: destination. Currently, we just send this to the bootstrap cpu */
	int bootstrap = 0; //HACK: we're assuming BSP is ID 0
	higher |= (bootstrap << 24) & 0xF;
	write_ioapic(l, irq*2 + 0x10, 0x10000);
	write_ioapic(l, irq*2 + 0x10 + 1, higher);
	write_ioapic(l, irq*2 + 0x10, lower);
}

void arch_interrupt_mask(int v)
{
	int vector = v - 32;
	for(int i=0;i<MAX_IOAPICS;i++) {
		struct ioapic *chip = &ioapics[i];
		if(chip->id == -1)
			continue;
		if(vector >= chip->gsib && vector < chip->gsib + 24) {
			/* TODO: this is a hack to distingish which interrupts should be level or edge triggered. There are
			 * "correct" ways of doing this. */
        	write_ioapic_vector(chip, vector, 1, vector+chip->gsib > 4 ? 1 : 0, 0, 0, 32+vector+chip->gsib);
		}
	}
}

void arch_interrupt_unmask(int v)
{
	int vector = v - 32;
	for(int i=0;i<MAX_IOAPICS;i++) {
		struct ioapic *chip = &ioapics[i];
		if(chip->id == -1)
			continue;
		if(vector >= chip->gsib && vector < chip->gsib + 24) {
        	write_ioapic_vector(chip, vector, 0, vector+chip->gsib > 4 ? 1 : 0, 0, 0, 32+vector+chip->gsib);
		}
	}
}

__orderedinitializer(__orderedbefore(MADT_INITIALIZER_ORDER))
static void __ioapic_preinit(void)
{
	for(int i=0;i<MAX_IOAPICS;i++)
		ioapics[i].id = -1;
}

void do_init_ioapic(struct ioapic *chip)
{
    for(int i=0;i<24;i++) {
        write_ioapic_vector(chip, i, 1, 0, 0, 0, 32+i+chip->gsib);
    }
}

__orderedinitializer(__orderedafter(MADT_INITIALIZER_ORDER))
static void __ioapic_postinit(void)
{
	int found = 0;
	/* disable PIC */
    x86_64_outb(0xA1, 0xFF);
    x86_64_outb(0x21, 0xFF);

	for(int i=0;i<MAX_IOAPICS;i++) {
		if(ioapics[i].id != -1) {
			do_init_ioapic(&ioapics[i]);
			found = 1;
		}
	}
	if(!found)
		panic(0, "no IOAPIC found, don't know how to map interrupts!");
}

