#include <net/nic.h>
#include <printk.h>
#include <net/packet.h>
static struct kobj kobj_nic = KOBJ_DEFAULT(nic);

static struct kobj kobj_packet = KOBJ_DEFAULT(packet);

extern struct network_protocol network_protocol_ipv6;
struct network_protocol netprots[NETWORK_TYPE_NUM] = {
	[NETWORK_TYPE_IPV6] = &network_protocol_ipv6,
};

static void _nic_worker(struct worker *worker)
{
	struct nic *nic = worker_arg(worker);
	spinlock_acquire(&nic->lock);
	while(worker_notjoining(worker)) {
		if(nic->rxpending) {
			nic->driver->recv(nic);
		} else {
			struct blockpoint bp;
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&nic->bl, &bp);
			if(!nic->rxpending) {
				spinlock_release(&nic->lock);
				schedule();
				spinlock_acquire(&nic->lock);
			}
			blockpoint_cleanup(&bp);
		}
		/* spinlock_release(&nic->lock); */
		/* schedule(); */
		/* spinlock_acquire(&nic->lock); */
	}
	spinlock_release(&nic->lock);
	worker_exit(worker, 0);
}

bool net_nic_match_netaddr(struct nic *nic, enum network_type type, uint8_t *addr, size_t length)
{
	__linkedlist_lock(&nic->addresses);
	for(struct linkedentry *entry = linkedlist_iter_start(&nic->addresses);
			entry != linkedlist_iter_end(&nic->addresses); entry = linkedlist_iter_next(entry)) {
		struct network_address *na = linkedentry_obj(entry);
		if(length == na->length && type == na->type && !memcmp(addr, na->address, length)) {
			__linkedlist_unlock(&nic->addresses);
			return true;
		}
	}
	__linkedlist_unlock(&nic->addresses);
	return false;
}

void net_nic_send(struct nic *nic, struct packet *packet)
{
	spinlock_acquire(&nic->lock);
	nic->driver->send(nic, packet);
	spinlock_release(&nic->lock);
}

void net_nic_receive(struct nic *nic, void *data, size_t length, int flags)
{
	(void)flags;
	(void)nic;
	(void)data;

	struct packet *packet = kobj_allocate(&kobj_packet);
	packet->data = data;
	packet->length = length;
	packet->origin = nic;
	/* simple link-layer router, for now */
	switch(nic->driver->type) {
		case NIC_TYPE_ETHERNET:
			net_ethernet_receive(packet);
			break;
	}
}

struct nic *net_nic_init(void *data, struct nic_driver *drv)
{
	struct nic *nic = kobj_allocate(&kobj_nic);
	nic->data = data;
	nic->driver = drv;
	spinlock_create(&nic->lock);
	nic->rxpending = false;
	linkedlist_create(&nic->addresses, 0);
	blocklist_create(&nic->bl);
	worker_start(&nic->worker, _nic_worker, nic);
	return nic;
}

