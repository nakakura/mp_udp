#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif


static unsigned int header_offset = (4 + 1 + 1 + 2 + 7 + 1 + 16 + 32 + 32) / sizeof(char);

typedef struct rtp_header
{
    uint16_t csrccount:4;
    uint16_t extension:1;
    uint16_t padding:1;
    uint16_t version:2;
    uint16_t type:7;
    uint16_t markerbit:1;
    uint16_t seq_number;
    uint32_t timestamp;
    uint32_t ssrc;
    uint32_t csrc[16];
} rtp_header;

uint16_t csrccount(void *src){
    rtp_header *rtp = (rtp_header*) src;
    return rtp->csrccount;
}

uint16_t extension(void *src){
    rtp_header *rtp = (rtp_header*) src;
    return rtp->extension;
}

void set_padding(void *src, uint16_t padding){
    rtp_header *rtp = (rtp_header*) src;
    rtp->padding = padding;
}

uint16_t padding(void *src){
    rtp_header *rtp = (rtp_header*) src;
    return rtp->padding;
}

uint16_t version(void *src){
    rtp_header *rtp = (rtp_header*) src;
    return rtp->version;
}

uint16_t type(void *src){
    rtp_header *rtp = (rtp_header*) src;
    return rtp->type;
}

uint16_t markerbit(void *src){
    rtp_header *rtp = (rtp_header*) src;
    return rtp->markerbit;
}

uint16_t seq_number(void *src){
    rtp_header *rtp = (rtp_header*) src;
    return rtp->seq_number;
}

uint32_t timestamp(void *src){
    rtp_header *rtp = (rtp_header*) src;
    uint32_t timestamp = rtp->timestamp;
    return ntohl(timestamp);
}

uint16_t ssrc(void *src){
    rtp_header *rtp = (rtp_header*) src;
    return rtp->ssrc;
}

//bytes
unsigned short header_length(char* src){
    rtp_header *rtp = (rtp_header*) src;
    return (header_offset + sizeof(uint32_t) * rtp->csrccount / sizeof(char)) / 8;
}

void split_rtp(char* src, unsigned short length, char* header, char* payload){
    int header_len = header_length(src);
    memcpy(header, src, sizeof(char) * header_len);
    memcpy(payload, &src[header_len], sizeof(char) * (length - header_len));
}

typedef struct janus_rtp_header_extension {
    uint16_t type;
    uint16_t length;
} janus_rtp_header_extension;

char *janus_rtp_payload(char *buf, int len, int *plen) {
    if(!buf || len < 12)
        return NULL;

    rtp_header *rtp = (rtp_header *)buf;
    int hlen = 12;
    if(rtp->csrccount)	/* Skip CSRC if needed */
        hlen += rtp->csrccount*4;

    if(rtp->extension) {
        janus_rtp_header_extension *ext = (janus_rtp_header_extension*)(buf+hlen);
        int extlen = ntohs(ext->length)*4;
        hlen += 4;
        if(len > (hlen + extlen))
            hlen += extlen;
    }
    if(plen)
        *plen = len-hlen;
    return buf+hlen;
}

#if defined(__ppc__) || defined(__ppc64__)
# define swap2(d)  \
	((d&0x000000ff)<<8) |  \
	((d&0x0000ff00)>>8)
#else
# define swap2(d) d
#endif

int is_keyframe(int codec, char* buffer, int len) {
    if(codec == 0) { //VP8
        /* VP8 packet */
        if(!buffer || len < 28)
            return 0;
        /* Parse RTP header first */
        rtp_header *header = (rtp_header *)buffer;
        uint32_t timestamp = ntohl(header->timestamp);
        uint16_t seq = ntohs(header->seq_number);
        int plen = 0;
        buffer = janus_rtp_payload(buffer, len, &plen);
        if(!buffer) {
            return 0;
        }
        /* Parse VP8 header now */
        uint8_t vp8pd = *buffer;
        uint8_t xbit = (vp8pd & 0x80);
        uint8_t sbit = (vp8pd & 0x10);
        if(xbit) {
            /* Read the Extended control bits octet */
            buffer++;
            vp8pd = *buffer;
            uint8_t ibit = (vp8pd & 0x80);
            uint8_t lbit = (vp8pd & 0x40);
            uint8_t tbit = (vp8pd & 0x20);
            uint8_t kbit = (vp8pd & 0x10);
            if(ibit) {
                /* Read the PictureID octet */
                buffer++;
                vp8pd = *buffer;
                uint16_t picid = vp8pd, wholepicid = picid;
                uint8_t mbit = (vp8pd & 0x80);
                if(mbit) {
                    memcpy(&picid, buffer, sizeof(uint16_t));
                    wholepicid = ntohs(picid);
                    picid = (wholepicid & 0x7FFF);
                    buffer++;
                }
            }
            if(lbit) {
                /* Read the TL0PICIDX octet */
                buffer++;
                vp8pd = *buffer;
            }
            if(tbit || kbit) {
                /* Read the TID/KEYIDX octet */
                buffer++;
                vp8pd = *buffer;
            }
            buffer++;	/* Now we're in the payload */
            if(sbit) {
                unsigned long int vp8ph = 0;
                memcpy(&vp8ph, buffer, 4);
                vp8ph = ntohl(vp8ph);
                uint8_t pbit = ((vp8ph & 0x01000000) >> 24);
                if(!pbit) {
                    /* It is a key frame! Get resolution for debugging */
                    unsigned char *c = (unsigned char *)buffer+3;
                    /* vet via sync code */
                    if(c[0]!=0x9d||c[1]!=0x01||c[2]!=0x2a) {
                    } else {
                        int vp8w = swap2(*(unsigned short*)(c+3))&0x3fff;
                        int vp8ws = swap2(*(unsigned short*)(c+3))>>14;
                        int vp8h = swap2(*(unsigned short*)(c+5))&0x3fff;
                        int vp8hs = swap2(*(unsigned short*)(c+5))>>14;
                        return 1;
                    }
                }
            }
        }
        /* If we got here it's not a key frame */
        return 0;
    } else if(codec == 1) { //VP9
        /* Parse RTP header first */
        rtp_header *header = (rtp_header *)buffer;
        uint32_t timestamp = ntohl(header->timestamp);
        uint16_t seq = ntohs(header->seq_number);
        int plen = 0;
        buffer = janus_rtp_payload(buffer, len, &plen);
        if(!buffer) {
            return 0;
        }
        /* Parse VP9 header now */
        uint8_t vp9pd = *buffer;
        uint8_t ibit = (vp9pd & 0x80);
        uint8_t pbit = (vp9pd & 0x40);
        uint8_t lbit = (vp9pd & 0x20);
        uint8_t fbit = (vp9pd & 0x10);
        uint8_t vbit = (vp9pd & 0x02);
        buffer++;
        if(ibit) {
            /* Read the PictureID octet */
            vp9pd = *buffer;
            uint16_t picid = vp9pd, wholepicid = picid;
            uint8_t mbit = (vp9pd & 0x80);
            if(!mbit) {
                buffer++;
            } else {
                memcpy(&picid, buffer, sizeof(uint16_t));
                wholepicid = ntohs(picid);
                picid = (wholepicid & 0x7FFF);
                buffer += 2;
            }
        }
        if(lbit) {
            buffer++;
            if(!fbit) {
                /* Non-flexible mode, skip TL0PICIDX */
                buffer++;
            }
        }
        if(fbit && pbit) {
            /* Skip reference indices */
            uint8_t nbit = 1;
            while(nbit) {
                vp9pd = *buffer;
                nbit = (vp9pd & 0x01);
                buffer++;
            }
        }
        if(vbit) {
            /* Parse and skip SS */
            vp9pd = *buffer;
            uint n_s = (vp9pd & 0xE0) >> 5;
            n_s++;
            uint8_t ybit = (vp9pd & 0x10);
            if(ybit) {
                /* Iterate on all spatial layers and get resolution */
                buffer++;
                uint i=0;
                for(i=0; i<n_s; i++) {
                    /* Width */
                    uint16_t *w = (uint16_t *)buffer;
                    int vp9w = ntohs(*w);
                    buffer += 2;
                    /* Height */
                    uint16_t *h = (uint16_t *)buffer;
                    int vp9h = ntohs(*h);
                    buffer += 2;
                    return 1;
                }
            }
        }
        /* If we got here it's not a key frame */
        return 0;
    } else if(codec == 2) { //H.264
        /* Parse RTP header first */
        rtp_header *header = (rtp_header *)buffer;
        uint32_t timestamp = ntohl(header->timestamp);
        uint16_t seq = ntohs(header->seq_number);
        int plen = 0;
        buffer = janus_rtp_payload(buffer, len, &plen);
        if(!buffer) {
            return 0;
        }
        /* Parse H264 header now */
        uint8_t fragment = *buffer & 0x1F;
        uint8_t nal = *(buffer+1) & 0x1F;
        uint8_t start_bit = *(buffer+1) & 0x80;
        if(fragment == 5 ||
           ((fragment == 28 || fragment == 29) && nal == 5 && start_bit == 128)) {
            return 1;
        }
        /* If we got here it's not a key frame */
        return 0;
    } else {
        /* FIXME Not a clue */
        return 0;
    }
}
