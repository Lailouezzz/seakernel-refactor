
#include "ahci.h"
#include <mutex.h>
#include <printk.h>
#include <sys.h>
#include <mmu.h>

uint32_t ahci_flush_commands(struct hba_port *port)
{
	/* the commands may not take effect until the command
	 * register is read again by software, because reasons.
	 */
	volatile uint32_t c = port->command;
	c=c;
	return c;
}

void ahci_stop_port_command_engine(volatile struct hba_port *port)
{
	port->command &= ~HBA_PxCMD_ST;
	port->command &= ~HBA_PxCMD_FRE;
	while((port->command & HBA_PxCMD_CR) || (port->command & HBA_PxCMD_FR))
		cpu_pause();
}

void ahci_start_port_command_engine(volatile struct hba_port *port)
{
	while(port->command & HBA_PxCMD_CR);
	port->command |= HBA_PxCMD_FRE;
	port->command |= HBA_PxCMD_ST; 
	ahci_flush_commands((struct hba_port *)port);
}

void ahci_reset_device(struct hba_memory *abar, struct hba_port *port, struct ahci_device *dev)
{
	(void)abar;
	/* TODO: This needs to clear out old commands and lock properly so that new commands can't get sent
	 * while the device is resetting */
	printk("[ahci]: device %d: sending COMRESET and reinitializing\n", dev->idx);
	ahci_stop_port_command_engine(port);
	port->sata_error = ~0;
	/* power on, spin up */
	port->command |= 2;
	port->command |= 4;
	ahci_flush_commands(port);
	struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000 };
	sys_nanosleep(&ts, NULL);
	/* initialize state */
	port->interrupt_status = ~0; /* clear pending interrupts */
	port->interrupt_enable = AHCI_DEFAULT_INT; /* we want some interrupts */
	port->command &= ~((1 << 27) | (1 << 26)); /* clear some bits */
	port->sata_control |= 1;
	ts.tv_nsec = 10000000;
	sys_nanosleep(&ts, NULL);
	port->sata_control |= (~1);
	sys_nanosleep(&ts, NULL);
	port->interrupt_status = ~0; /* clear pending interrupts */
	port->interrupt_enable = AHCI_DEFAULT_INT; /* we want some interrupts */
	ahci_start_port_command_engine(port);
	dev->slots=0;
	port->sata_error = ~0;
}

uint32_t ahci_get_previous_byte_count(struct hba_memory *abar, struct hba_port *port, struct ahci_device *dev, int slot)
{
	(void)abar;
	(void)port;
	struct hba_command_header *h = (struct hba_command_header *)dev->clb_virt;
	h += slot;
	return h->prdb_count;
}


int ahci_initialize_device(struct hba_memory *abar, struct ahci_device *dev)
{
	printk("[ahci]: initializing device %d\n", dev->idx);
	struct hba_port *port = (struct hba_port *)&abar->ports[dev->idx];
	ahci_stop_port_command_engine(port);
	port->sata_error = ~0;
	/* power on, spin up */
	port->command |= (2 | 4);
	ahci_flush_commands(port);
	struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000 };
	sys_nanosleep(&ts, NULL);
	/* initialize state */
	port->interrupt_status = ~0; /* clear pending interrupts */
	port->interrupt_enable = AHCI_DEFAULT_INT; /* we want some interrupts */
	
	port->command &= ~1;
	while(port->command & (1 << 15)) cpu_pause();
	port->command &= ~((1 << 27) | (1 << 26) | 1); /* clear some bits */
	ahci_flush_commands(port);
	port->sata_control |= 1;
	ts.tv_nsec = 10000000;
	sys_nanosleep(&ts, NULL);
	port->sata_control |= (~1);
	sys_nanosleep(&ts, NULL);
	while(!(port->sata_status & 1)) cpu_pause();
	port->sata_error = ~0;
	port->command |= (1 << 28); /* set interface to active */
	while((port->sata_status >> 8) != 1) cpu_pause();
	port->interrupt_status = ~0; /* clear pending interrupts */
	port->interrupt_enable = AHCI_DEFAULT_INT; /* we want some interrupts */
	/* map memory */
	
	dev->dma_clb = mm_physical_allocate(0x1000, false);
	dev->dma_fis = mm_physical_allocate(0x1000, false);

	dev->clb_virt = (void *)(dev->dma_clb + PHYS_MAP_START);
	dev->fis_virt = (void *)(dev->dma_fis + PHYS_MAP_START);
	dev->slots=0;
	struct hba_command_header *h = (struct hba_command_header *)dev->clb_virt;
	int i;
	for(i=0;i<HBA_COMMAND_HEADER_NUM;i++) {
		dev->ch_dmas[i] = mm_physical_allocate(0x1000, false);
		dev->ch[i] = (void *)(dev->ch_dmas[i] + PHYS_MAP_START);
		memset(h, 0, sizeof(*h));
		h->command_table_base_l = LOWER32(dev->ch_dmas[i]);
		h->command_table_base_h = UPPER32(dev->ch_dmas[i]);
		h++;
	}
	
	port->command_list_base_l = LOWER32(dev->dma_clb);
	port->command_list_base_h = UPPER32(dev->dma_clb);
	
	port->fis_base_l = LOWER32(dev->dma_fis);
	port->fis_base_h = UPPER32(dev->dma_fis);
 	ahci_start_port_command_engine(port);
	port->sata_error = ~0;
	return ahci_device_identify_ahci(abar, port, dev);
}

uint32_t ahci_check_type(volatile struct hba_port *port)
{
	port->command &= ~1;
	while(port->command & (1 << 15)) cpu_pause();
	port->command &= ~(1 << 4);
	while(port->command & (1 << 14)) cpu_pause();
	atomic_thread_fence(memory_order_seq_cst);
	port->command |= 2;
	atomic_thread_fence(memory_order_seq_cst);
	struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000 };
	sys_nanosleep(&ts, NULL);

	uint32_t s = port->sata_status;

	printk("[ahci]: port data: sig=%x, stat=%x, ctl=%x, sac=%x\n", port->signature, s, port->command, port->sata_active);
	uint8_t ipm, det;
	ipm = (s >> 8) & 0x0F;
	det = s & 0x0F;
	printk("[ahci]: port check: ipm=%x, det=%x\n", ipm, det);
	if(ipm != 1 || det != 3)
		return 0;
	return port->signature;
}

struct kobj kobj_hba_port = KOBJ_DEFAULT(hba_port);

void ahci_probe_ports(struct hba_memory *abar)
{
	uint32_t pi = abar->port_implemented;
	printk("[ahci]: ports implemented: %x\n", pi);
	int i=0;
	while(i < 32)
	{
		if(pi & 1)
		{
			uint32_t type = ahci_check_type(&abar->ports[i]);
			if(type) {
				printk("[ahci]: detected device on port %d\n", i);
				ports[i] = kobj_allocate(&kobj_hba_port);
				ports[i]->type = type;
				ports[i]->idx = i;
				mutex_create(&(ports[i]->lock));
				if(ahci_initialize_device(abar, ports[i]))
					ahci_create_device(ports[i]);
				else
					printk("[ahci]: failed to initialize device %d, disabling port\n", i);
			}
		}
		i++;
		pi >>= 1;
	}
}

void ahci_init_hba(struct hba_memory *abar)
{
	if(abar->ext_capabilities & 1) {
		/* request BIOS/OS ownership handoff */
		printk("[ahci]: requesting AHCI ownership change\n");
		abar->bohc |= (1 << 1);
		while((abar->bohc & 1) || !(abar->bohc & (1<<1))) cpu_pause();
		printk("[ahci]: ownership change completed\n");
	}
	
	/* enable the AHCI and reset it */
	abar->global_host_control |= HBA_GHC_AHCI_ENABLE;
	abar->global_host_control |= HBA_GHC_RESET;
	/* wait for reset to complete */
	while(abar->global_host_control & HBA_GHC_RESET) cpu_pause();
	/* enable the AHCI and interrupts */
	abar->global_host_control |= HBA_GHC_AHCI_ENABLE;
	abar->global_host_control |= HBA_GHC_INTERRUPT_ENABLE;
	struct timespec ts = {.tv_sec = 0, .tv_nsec = 20000000 };
	sys_nanosleep(&ts, NULL);
	printk("[ahci]: caps and ver: %x %x v %x, ctl: %x\n", abar->capability, abar->ext_capabilities, abar->version, abar->global_host_control);
}
