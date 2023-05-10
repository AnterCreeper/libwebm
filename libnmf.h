#ifndef __LIBNMF_INCLUDE__
#define __LIBNMF_INCLUDE__

#define _FILE_OFFSET_BITS 64

#undef __STRICT_ANSI__
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
	#define EXTERN_C extern "C"
#else
	#define EXTERN_C
#endif

#define NMF_INDICATOR 0xFF

#define NMF_HEADER 0x01
#define NMF_TRACK 0x02
#define NMF_INDEX 0x03

#define NMF_MAGIC_NUM 0x46454D4E

#define NMF_TRACK_UNKNOWN 0x00
#define NMF_TRACK_VIDEO 0x01
#define NMF_TRACK_AUDIO 0x02

#define NMF_VIDEO_MJPG 0x47504A4D
#define NMF_AUDIO_FLAC 0x43614C66

#define NMF_INDEX_MAX 32768

struct nmf_header {
	float duration;
	uint32_t track_num;
};

struct nmf_track_header {
	uint8_t index;
	uint8_t type;
	uint16_t reserved;
	uint32_t codec;
};

struct nmf_track {
	struct nmf_track_header header;
	uint32_t length; //in word
	uint32_t* payload;
};

struct nmf_index {
	uint32_t fp;
	//file position of cue index.
	//if zero, it means we don't use index, just serializing.
	uint32_t scale;
	//stamp scale factor. in ns.
	uint32_t count;
	//how many clusters we have.
};

struct nmf_container{
	struct nmf_header header;
	struct nmf_track* tracks;
	struct nmf_index index;
};

struct nmf_cluster_header {
	uint32_t stamp;
	uint32_t frame_num;
};

struct nmf_frames {
	uint32_t tag;
	uint32_t* payload;
};

struct nmf_cluster {
	struct nmf_cluster_header header;
	struct nmf_frames* frames;
};

void parse_header(uint32_t* buffer, size_t length, struct nmf_header* content);
void parse_track(uint32_t* buffer, size_t length, struct nmf_track* content);
void parse_index(uint32_t* buffer, uint32_t length, struct nmf_index* content);
void parse_nmf(uint32_t* buffer, uint32_t length, struct nmf_container* content);

EXTERN_C uint64_t read_nmf(FILE* fd, struct nmf_container* container);
EXTERN_C uint32_t read_nmf_cluster(FILE* fd, struct nmf_cluster* content);

EXTERN_C void write_nmf(FILE* fd, struct nmf_container* container, uint32_t* pos_index);
EXTERN_C void write_nmf_cluster(FILE* fd, struct nmf_cluster* content);

#define MJPG_FMT_YUV444 0x00
#define MJPG_FMT_YUV422 0x01
#define MJPG_FMT_YUV420 0x02
#define MJPG_FMT_GREY	0x03
#define MJPG_FMT_DQT_D	0x04

struct jfif_container {
	uint16_t width;
	uint16_t height;
	uint32_t format;
	uint32_t interval; //in ns
};

void jfif_parse(struct jfif_container* content, uint32_t* buffer, uint32_t length);
void flac_parse(uint32_t* content, uint32_t length);

#endif
