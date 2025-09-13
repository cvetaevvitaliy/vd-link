// RFC6184 RTP Payload Format for H.264 Video

#include "rtp-packet.h"
#include "rtp-payload-internal.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#define H264_NAL(v)	((v) & 0x1F)
#define FU_START(v) ((v) & 0x80)
#define FU_END(v)	((v) & 0x40)
#define FU_NAL(v)	((v) & 0x1F)

struct rtp_decode_h264_t
{
	struct rtp_payload_t handler;
	void* cbparam;

	uint16_t seq; // rtp seq
	uint32_t timestamp;

	uint8_t* ptr;
	int size, capacity;

	int flags;
};

static void* rtp_h264_unpack_create(struct rtp_payload_t *handler, void* param)
{
	struct rtp_decode_h264_t *unpacker;
	unpacker = (struct rtp_decode_h264_t *)calloc(1, sizeof(*unpacker));
	if(!unpacker)
		return NULL;

	memcpy(&unpacker->handler, handler, sizeof(unpacker->handler));
	unpacker->cbparam = param;
	unpacker->flags = -1;
	return unpacker;
}

static void rtp_h264_unpack_destroy(void* p)
{
	struct rtp_decode_h264_t *unpacker;
	unpacker = (struct rtp_decode_h264_t *)p;

	if(unpacker->ptr)
		free(unpacker->ptr);
#if defined(_DEBUG) || defined(DEBUG)
	memset(unpacker, 0xCC, sizeof(*unpacker));
#endif
	free(unpacker);
}

// 5.7.1. Single-Time Aggregation Packet (STAP) (p23)
/*
 0               1               2               3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           RTP Header                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|STAP-B NAL HDR |            DON                |  NALU 1 Size  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| NALU 1 Size   | NALU 1 HDR    |         NALU 1 Data           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
:                                                               :
+               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|               | NALU 2 Size                   |   NALU 2 HDR  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                            NALU 2 Data                        |
:                                                               :
|                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               :    ...OPTIONAL RTP padding    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
//static int rtp_h264_unpack_stap(struct rtp_decode_h264_t *unpacker, const uint8_t* ptr, int bytes, uint32_t timestamp, int stap_b)
//{
//	int r, n;
//	uint16_t len;
//	uint16_t don;
//
//	r = 0;
//	n = stap_b ? 3 : 1;
//    if (bytes < n)
//    {
//        assert(0);
//        return -EINVAL; // error
//    }
//	don = stap_b ? nbo_r16(ptr + 1) : 0;
//	ptr += n; // STAP-A / STAP-B HDR + DON
//
//	for(bytes -= n; 0 == r && bytes > 2; bytes -= len + 2)
//	{
//		len = nbo_r16(ptr);
//		if(len + 2 > bytes || len < 2)
//		{
//			assert(0);
//			unpacker->flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
//			unpacker->size = 0;
//			return -EINVAL; // error
//		}
//
//		assert(H264_NAL(ptr[2]) > 0 && H264_NAL(ptr[2]) < 24);
//		//r = unpacker->handler.packet(unpacker->cbparam, ptr + 2, len, timestamp, unpacker->flags);
//        // +4 для 00 00 00 01
//        uint8_t* nal_buf = malloc(len + 4);
//        memcpy(nal_buf, "\x00\x00\x00\x01", 4);
//        memcpy(nal_buf + 4, ptr + 2, len);
//        r = unpacker->handler.packet(unpacker->cbparam, nal_buf, len + 4, timestamp, unpacker->flags);
//        free(nal_buf);
//
//		unpacker->flags = 0;
//		unpacker->size = 0;
//
//		ptr += len + 2; // next NALU
//		don = (don + 1) % 65536;
//	}
//
//	return 0 == r ? 1 : r; // packet handled
//}

static int rtp_h264_unpack_stap(struct rtp_decode_h264_t *unpacker, const uint8_t* ptr, int bytes, uint32_t timestamp, int stap_b)
{
    int r = 0, n = stap_b ? 3 : 1;
    if (bytes < n) return -EINVAL;
    ptr += n;
    bytes -= n;

    static const uint8_t start_code[4] = {0, 0, 0, 1};

    while (bytes > 2) {
        uint16_t len = nbo_r16(ptr);
        if (len + 2 > bytes || len < 2) break;

        r = unpacker->handler.packet(unpacker->cbparam, start_code, 4, timestamp, unpacker->flags);
        if (r == 0)
            r = unpacker->handler.packet(unpacker->cbparam, ptr + 2, len, timestamp, unpacker->flags);

        unpacker->flags = 0;
        unpacker->size = 0;

        ptr += len + 2;
        bytes -= (len + 2);
    }
    return 0 == r ? 1 : r;
}


// 5.7.2. Multi-Time Aggregation Packets (MTAPs) (p27)
/*
 0               1               2               3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                          RTP Header                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|MTAP16 NAL HDR |   decoding order number base  |  NALU 1 Size  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| NALU 1 Size   | NALU 1 DOND   |         NALU 1 TS offset      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| NALU 1 HDR    |                NALU 1 DATA                    |
+-+-+-+-+-+-+-+-+                                               +
:                                                               :
+               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|               | NALU 2 SIZE                   |   NALU 2 DOND |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| NALU 2 TS offset              | NALU 2 HDR    |  NALU 2 DATA  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+               |
:                                                               :
|                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               :    ...OPTIONAL RTP padding    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
static int rtp_h264_unpack_mtap(struct rtp_decode_h264_t *unpacker, const uint8_t* ptr, int bytes, uint32_t timestamp, int n)
{
	int r;
	//uint16_t dond;
	uint16_t donb;
	uint16_t len;
	uint32_t ts;

	r = 0;
    if (bytes < 3)
    {
        assert(0);
        return -EINVAL; // error
    }
    
	donb = nbo_r16(ptr + 1);
	ptr += 3; // MTAP16/MTAP24 HDR + DONB

	for(bytes -= 3; 0 == r && n + 3 < bytes; bytes -= len + 2)
	{
		len = nbo_r16(ptr);
		if(len + 2 > bytes || len < 1 /*DOND*/ + n /*TS offset*/ + 1 /*NALU*/)
		{
			assert(0);
			unpacker->flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
			unpacker->size = 0;
			return -EINVAL; // error
		}

		//dond = (ptr[2] + donb) % 65536;
		ts = (uint16_t)nbo_r16(ptr + 3);
		if (3 == n) ts = (ts << 8) | ptr[5]; // MTAP24

		// if the NALU-time is larger than or equal to the RTP timestamp of the packet, 
		// then the timestamp offset equals (the NALU - time of the NAL unit - the RTP timestamp of the packet).
		// If the NALU - time is smaller than the RTP timestamp of the packet,
		// then the timestamp offset is equal to the NALU - time + (2 ^ 32 - the RTP timestamp of the packet).
		ts += timestamp; // wrap 1 << 32

		assert(H264_NAL(ptr[n + 3]) > 0 && H264_NAL(ptr[n + 3]) < 24);
		r = unpacker->handler.packet(unpacker->cbparam, ptr + 2 + 1 + n, len - 1 - n, ts, unpacker->flags);
		unpacker->flags = 0;
		unpacker->size = 0;

		ptr += len + 2; // next NALU
	}

	return 0 == r ? 1 : r; // packet handled
}

// 5.8. Fragmentation Units (FUs) (p29)
/*
 0               1               2               3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  FU indicator |   FU header   |              DON              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
|                                                               |
|                          FU payload                           |
|                                                               |
|                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               :   ...OPTIONAL RTP padding     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
static int rtp_h264_unpack_fu(struct rtp_decode_h264_t *unpacker, const uint8_t* ptr, int bytes, uint32_t timestamp, int fu_b)
{
    int r = 0, n;
    uint8_t fuheader;

    n = fu_b ? 4 : 2;
    if (bytes < n) return -EINVAL;
    if (unpacker->size + bytes - n > RTP_PAYLOAD_MAX_SIZE) return -EINVAL;

    if (unpacker->size + bytes - n + 1 > unpacker->capacity) {
        int newcap = unpacker->size + bytes + 1;
        newcap += (newcap/4 > 128000) ? newcap/4 : 128000;
        void* p = realloc(unpacker->ptr, newcap);
        if (!p) {
            unpacker->flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
            unpacker->size = 0;
            return -ENOMEM;
        }
        unpacker->ptr = (uint8_t*)p;
        unpacker->capacity = newcap;
    }

    fuheader = ptr[1];
    if (FU_START(fuheader)) {
        unpacker->size = 1;
        unpacker->ptr[0] = (ptr[0] & 0xE0) | (fuheader & 0x1F); // NAL header
    } else {
        if (unpacker->size == 0) {
            unpacker->flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
            return 0;
        }
    }

    unpacker->timestamp = timestamp;

    if (bytes > n) {
        memcpy(unpacker->ptr + unpacker->size, ptr + n, bytes - n);
        unpacker->size += bytes - n;
    }

    // Callback FU-A!
    if (FU_END(fuheader)) {
        static const uint8_t start_code[4] = {0, 0, 0, 1};
        r = unpacker->handler.packet(unpacker->cbparam, start_code, 4, timestamp, unpacker->flags);
        if (r == 0)
            r = unpacker->handler.packet(unpacker->cbparam, unpacker->ptr, unpacker->size, timestamp, unpacker->flags);

        unpacker->flags = 0;
        unpacker->size = 0;
    }
    return 0 == r ? 1 : r;
}

//static int rtp_h264_unpack_fu(struct rtp_decode_h264_t *unpacker, const uint8_t* ptr, int bytes, uint32_t timestamp, int fu_b)
//{
//	int r, n;
//	uint8_t fuheader;
//	//uint16_t don;
//
//	r = 0;
//	n = fu_b ? 4 : 2;
//	if (bytes < n || unpacker->size + bytes - n > RTP_PAYLOAD_MAX_SIZE)
//	{
//		assert(0);
//		return -EINVAL; // error
//	}
//
//	if (unpacker->size + bytes - n + 1 /*NALU*/ > unpacker->capacity)
//	{
//		void* p = NULL;
//		int size = unpacker->size + bytes + 1;
//		size += size / 4 > 128000 ? size / 4 : 128000;
//		p = realloc(unpacker->ptr, size);
//		if (!p)
//		{
//			// set packet lost flag
//			unpacker->flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
//			unpacker->size = 0;
//			return -ENOMEM; // error
//		}
//		unpacker->ptr = (uint8_t*)p;
//		unpacker->capacity = size;
//	}
//
//	fuheader = ptr[1];
//	//don = nbo_r16(ptr + 2);
//	if (FU_START(fuheader))
//	{
//#if 0
//		if (unpacker->size > 0)
//		{
//			unpacker->flags |= RTP_PAYLOAD_FLAG_PACKET_CORRUPT;
//			unpacker->handler.packet(unpacker->cbparam, unpacker->ptr, unpacker->size, unpacker->timestamp, unpacker->flags);
//			unpacker->flags = 0;
//			unpacker->size = 0; // reset
//		}
//#endif
//
//		unpacker->size = 1; // NAL unit type byte
//		unpacker->ptr[0] = (ptr[0]/*indicator*/ & 0xE0) | (fuheader & 0x1F);
//		assert(H264_NAL(unpacker->ptr[0]) > 0 && H264_NAL(unpacker->ptr[0]) < 24);
//	}
//	else
//	{
//		if (0 == unpacker->size)
//		{
//			unpacker->flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
//			return 0; // packet discard
//		}
//		assert(unpacker->size > 0);
//	}
//
//	unpacker->timestamp = timestamp;
//	if (bytes > n)
//	{
//		assert(unpacker->capacity >= unpacker->size + bytes - n);
//		memmove(unpacker->ptr + unpacker->size, ptr + n, bytes - n);
//		unpacker->size += bytes - n;
//	}
//
////	if(FU_END(fuheader))
////	{
////        if(unpacker->size > 0)
////            r = unpacker->handler.packet(unpacker->cbparam, unpacker->ptr, unpacker->size, timestamp, unpacker->flags);
////		unpacker->flags = 0;
////		unpacker->size = 0; // reset
////	}
//    if(FU_END(fuheader))
//    {
//        if(unpacker->size > 0) {
//            uint8_t* nal_buf = malloc(unpacker->size + 4);
//            memcpy(nal_buf, "\x00\x00\x00\x01", 4);
//            memcpy(nal_buf+4, unpacker->ptr, unpacker->size);
//            r = unpacker->handler.packet(unpacker->cbparam, nal_buf, unpacker->size + 4, timestamp, unpacker->flags);
//            free(nal_buf);
//        }
//        unpacker->flags = 0;
//        unpacker->size = 0; // reset
//    }
//
//	return 0 == r ? 1 : r; // packet handled
//}

//static int rtp_h264_unpack_input(void* p, const void* packet, int bytes)
//{
//	int r;
//	uint8_t nalt;
//	struct rtp_packet_t pkt;
//	struct rtp_decode_h264_t *unpacker;
//
//	unpacker = (struct rtp_decode_h264_t *)p;
//	if(!unpacker || 0 != rtp_packet_deserialize(&pkt, packet, bytes) || pkt.payloadlen < 1)
//		return -EINVAL;
//
//	if (-1 == unpacker->flags)
//	{
//		unpacker->flags = 0;
//		unpacker->seq = (uint16_t)(pkt.rtp.seq - 1); // disable packet lost
//	}
//
//	if ((uint16_t)pkt.rtp.seq != (uint16_t)(unpacker->seq + 1))
//	{
//		unpacker->flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
//		unpacker->size = 0; // discard previous packets
//	}
//	unpacker->seq = (uint16_t)pkt.rtp.seq;
//
//	nalt = ((unsigned char *)pkt.payload)[0];
//	switch(nalt & 0x1F)
//	{
//	case 0: // reserved
//	case 31: // reserved
//		assert(0);
//		return 0; // packet discard
//
//	case 24: // STAP-A
//		return rtp_h264_unpack_stap(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 0);
//	case 25: // STAP-B
//		return rtp_h264_unpack_stap(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 1);
//	case 26: // MTAP16
//		return rtp_h264_unpack_mtap(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 2);
//	case 27: // MTAP24
//		return rtp_h264_unpack_mtap(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 3);
//	case 28: // FU-A
//		return rtp_h264_unpack_fu(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 0);
//	case 29: // FU-B
//		return rtp_h264_unpack_fu(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 1);
//
//	default: // 1-23 NAL unit
//		r = unpacker->handler.packet(unpacker->cbparam, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, unpacker->flags);
//		unpacker->flags = 0;
//		unpacker->size = 0;
//		return 0 == r ? 1 : r; // packet handled
//	}
//}

static int rtp_h264_unpack_input(void* p, const void* packet, int bytes)
{
    int r = 0;
    uint8_t nalt;
    struct rtp_packet_t pkt;
    struct rtp_decode_h264_t *unpacker;

    unpacker = (struct rtp_decode_h264_t *)p;
    if(!unpacker || 0 != rtp_packet_deserialize(&pkt, packet, bytes) || pkt.payloadlen < 1)
        return -EINVAL;

    if (-1 == unpacker->flags)
    {
        unpacker->flags = 0;
        unpacker->seq = (uint16_t)(pkt.rtp.seq - 1); // disable packet lost
    }

    if ((uint16_t)pkt.rtp.seq != (uint16_t)(unpacker->seq + 1))
    {
        unpacker->flags = RTP_PAYLOAD_FLAG_PACKET_LOST;
        unpacker->size = 0; // discard previous packets
    }
    unpacker->seq = (uint16_t)pkt.rtp.seq;

    nalt = ((unsigned char *)pkt.payload)[0];
    switch(nalt & 0x1F)
    {
        case 0: // reserved
        case 31: // reserved
            //assert(0);
            return 0; // packet discard

        case 24: // STAP-A ( NAL)
        case 25: // STAP-B ( NAL)
        {
            int n = (nalt & 0x1F) == 25 ? 3 : 1;
            const uint8_t *ptr = ((const uint8_t*)pkt.payload) + n;
            int bytes_left = pkt.payloadlen - n;

            while (bytes_left > 2) {
                uint16_t len = nbo_r16(ptr);
                if (len + 2 > bytes_left || len < 2)
                    break;

                static const uint8_t start_code[4] = {0, 0, 0, 1};
                r = unpacker->handler.packet(unpacker->cbparam, start_code, 4, pkt.rtp.timestamp, unpacker->flags);
                if (r == 0)
                    r = unpacker->handler.packet(unpacker->cbparam, ptr + 2, len, pkt.rtp.timestamp, unpacker->flags);

                unpacker->flags = 0;
                unpacker->size = 0;

                ptr += len + 2;
                bytes_left -= (len + 2);
            }
            return 0 == r ? 1 : r;
        }
        case 26: // MTAP16 ( NAL)
        case 27: // MTAP24 (NAL)
        {
            int n = (nalt & 0x1F) == 27 ? 3 : 2;
            int hdr_skip = 3;
            const uint8_t *ptr = ((const uint8_t*)pkt.payload) + hdr_skip;
            int bytes_left = pkt.payloadlen - hdr_skip;

            while (bytes_left > 2) {
                if (bytes_left < 2) break;
                uint16_t len = nbo_r16(ptr);
                if (len + 2 > bytes_left) break;

                int off = 2 + 1 + n; // len + DOND + ts_offset
                if (len < 1 + n) break;

                static const uint8_t start_code[4] = {0,0,0,1};
                r = unpacker->handler.packet(unpacker->cbparam, start_code, 4, pkt.rtp.timestamp, unpacker->flags);
                // Передаємо сам NAL
                if (r == 0)
                    r = unpacker->handler.packet(unpacker->cbparam, ptr + off, len - 1 - n, pkt.rtp.timestamp, unpacker->flags);

                unpacker->flags = 0;
                unpacker->size = 0;

                ptr += len + 2;
                bytes_left -= (len + 2);
            }
            return 0 == r ? 1 : r;
        }
        case 28: // FU-A
            return rtp_h264_unpack_fu(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 0);
        case 29: // FU-B
            return rtp_h264_unpack_fu(unpacker, (const uint8_t*)pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, 1);

        default: // 1-23 NAL unit ( NAL)
        {
            static const uint8_t start_code[4] = {0, 0, 0, 1};
            r = unpacker->handler.packet(unpacker->cbparam, start_code, 4, pkt.rtp.timestamp, unpacker->flags);
            if (r == 0)
                r = unpacker->handler.packet(unpacker->cbparam, pkt.payload, pkt.payloadlen, pkt.rtp.timestamp, unpacker->flags);
            unpacker->flags = 0;
            unpacker->size = 0;
            return 0 == r ? 1 : r;
        }
    }
}


struct rtp_payload_decode_t *rtp_h264_decode()
{
	static struct rtp_payload_decode_t unpacker = {
		rtp_h264_unpack_create,
		rtp_h264_unpack_destroy,
		rtp_h264_unpack_input,
	};

	return &unpacker;
}
