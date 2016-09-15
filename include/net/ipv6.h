#pragma once

struct packet;
enum {
	IP_PROTOCOL_ICMP6 = 0x3A,
};

union ipv6_address {
	uint8_t octets[16];
	struct {
		uint64_t prefix;
		uint64_t id;
	};
};

struct icmp6_header {
	uint8_t type;
	uint8_t code;
	uint16_t checksum;
	uint8_t data[];
};

struct ipv6_header {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint32_t tc0 : 4;
	uint32_t version : 4;
	uint32_t tcfl : 24;
#else
	uint32_t version : 4;
	uint32_t tcfl : 28;
#endif
	uint16_t length;
	uint8_t next_header;
	uint8_t hoplim;
	
	union ipv6_address source;
	union ipv6_address destination;
	uint8_t data[];
} __attribute__((packed));

void ipv6_receive(struct packet *packet, struct ipv6_header *header);

void icmp6_receive(struct packet *packet, struct ipv6_header *header);
