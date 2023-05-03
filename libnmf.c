#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libnmf.h"

void parse_header(uint32_t* buffer, size_t length, struct nmf_header* content) {
	if (length != sizeof(struct nmf_header) / 4) {
		printf("FIXME! wrong header size.\n");
		exit(1);
	}
	memcpy(content, buffer, sizeof(struct nmf_header));
	return;
}

void parse_track(uint32_t* buffer, size_t length, struct nmf_track* content) {
	if (content == NULL) {
		printf("FIXME! no track memory allocated!\n");
		exit(1);
	}
	struct nmf_track_header payload;
	memcpy(&payload, buffer, sizeof(struct nmf_track_header));
	memcpy(&content[payload.index].header, &payload, sizeof(struct nmf_track_header));
	buffer = buffer + sizeof(struct nmf_track_header) / 4;
	length = length - sizeof(struct nmf_track_header) / 4;
	if (length != 0) {
		content[payload.index].length = length;
		content[payload.index].payload = (uint32_t*)malloc(4 * length);
		memcpy(content[payload.index].payload, buffer, 4 * length);
	} else {
		content[payload.index].length = 0;
		content[payload.index].payload = NULL;
	}
	return;
}

void parse_index(uint32_t* buffer, uint32_t length, struct nmf_index* content) {
	if (length != sizeof(struct nmf_index) / 4) {
		printf("FIXME! wrong index size.\n");
		exit(1);
	}
	memcpy(content, buffer, sizeof(struct nmf_index));
	return;
}

void parse_nmf(uint32_t* buffer, uint32_t length, struct nmf_container* content) {
	int process = 0;
	while (process < length) {
		uint32_t tag = buffer[process];
		uint32_t start = tag & 0x000000FF;
		if (start != 0x000000FF) {
			printf("FIXME! not tag.\n");
			printf("data:%x\n", tag);
			exit(1);
		}
		uint32_t id = (tag >> 8) & 0x000000FF;
		uint32_t size = tag >> 16;
		switch (id) {
		case NMF_HEADER:
			process++;
			parse_header(&buffer[process], size, &content->header);
			content->tracks = malloc((content->header).track_num * sizeof(struct nmf_track));
			break;
		case NMF_TRACK:
			process++;
			parse_track(&buffer[process], size, content->tracks);
			break;
		case NMF_INDEX:
			process++;
			parse_index(&buffer[process], size, &content->index);
			break;
		default:
			printf("FIXME! unknown tag.\n");
			exit(1);
		}
		process = process + size;
	}
	return;
}

////////////////////////////////////////////////////////////////////////////////////////

uint64_t read_nmf(FILE* fd, struct nmf_container* container) {
	if (container == NULL) {
		printf("FIXME! NULL pointer of content.\n");
		exit(1);
	}

	fseeko(fd, 0, SEEK_END);
	uint64_t file_size = ftello(fd);
	if (file_size < 3) {
		printf("FIXME! wrong file size!\n");
		exit(1);
	}

	uint32_t magic_num;
	fseeko(fd, 0, SEEK_SET);
	fread(&magic_num, 4, 1, fd);
	if (magic_num == NMF_MAGIC_NUM) {
		uint32_t length;
		fread(&length, 4, 1, fd);
		uint32_t* payload;
		payload = malloc(4 * length);
		fread(payload, 4, length, fd);
		parse_nmf(payload, length, container);
		free(payload);
	} else {
		printf("FIXME: unknown file type!\n");
		exit(1);
	}
	return file_size;
}

void read_nmf_cluster(FILE* fd, struct nmf_container* containter, struct nmf_cluster* content) {
	uint32_t length;
	fread(&length, 4, 1, fd);
	fread(&content->header, 4, sizeof(struct nmf_cluster_header) / 4, fd);
	content->frames = malloc(sizeof(struct nmf_frames) * content->header.frame_num);
	for (int i = 0; i < content->header.frame_num; i++) {
		fread(&content->frames[i].tag, 4, 1, fd);
		uint32_t length = content->frames[i].tag >> 16;
		content->frames[i].payload = malloc(4 * length);
		fread(content->frames[i].payload, 4, length, fd);
	}
	return;
}

void write_nmf(FILE* fd, struct nmf_container* container, uint32_t* pos_index) {
	const uint32_t magic_num = NMF_MAGIC_NUM;
	fwrite(&magic_num, 4, 1, fd);

	uint32_t length_of_header =
			sizeof(struct nmf_header) / 4;
	uint32_t* length_of_track = malloc(sizeof(uint32_t) * container->header.track_num);
	for (int i = 0; i < container->header.track_num; i++) {
		length_of_track[i] =
			sizeof(struct nmf_track_header) / 4 + container->tracks[i].length;
	}
	uint32_t length_of_index =
			sizeof(struct nmf_index) / 4;

	uint32_t length_total = 1 + length_of_header + 1 + length_of_index;
	for (int i = 0; i < container->header.track_num; i++) length_total += 1 + length_of_track[i];
	fwrite(&length_total, 4, 1, fd);

	uint32_t tag_header = 0xFF;
	tag_header += NMF_HEADER << 8;
	tag_header += length_of_header << 16;
	fwrite(&tag_header, 4, 1, fd);
	fwrite(&container->header, 4, sizeof(struct nmf_header) / 4, fd);

	for (int i = 0; i < container->header.track_num; i++) {
		uint32_t tag_track = 0xFF;
		tag_track += NMF_TRACK << 8;
		tag_track += length_of_track[i] << 16;
		fwrite(&tag_track, 4, 1, fd);
		fwrite(&container->tracks[i].header, 4, sizeof(struct nmf_track_header) / 4, fd);
		fwrite(container->tracks[i].payload, 4, container->tracks[i].length, fd);
	}

	uint32_t tag_index = 0xFF;
	tag_index += NMF_INDEX << 8;
	tag_index += length_of_index << 16;
	fwrite(&tag_index, 4, 1, fd);
	*pos_index = ftell(fd);
	fwrite(&container->index, 4, sizeof(struct nmf_index) / 4, fd);

	return;
}

void write_nmf_cluster(FILE* fd, struct nmf_cluster* content) {
	uint32_t length_of_header =
		sizeof(struct nmf_cluster_header) / 4;
	uint32_t* length_of_frames = malloc(sizeof(struct nmf_frames) * content->header.frame_num);
	for (int i = 0; i < content->header.frame_num; i++) {
		length_of_frames[i] = ((content->frames[i].tag >> 8) + 3) / 4;
	}

	uint32_t length_total = length_of_header;
	for (int i = 0; i < content->header.frame_num; i++) length_total = length_total + 1 + length_of_frames[i];
	fwrite(&length_total, 4, 1, fd);

	fwrite(&content->header, 4, sizeof(struct nmf_cluster_header) / 4, fd);

	for (int i = 0; i < content->header.frame_num; i++) {
		uint32_t tag = content->frames[i].tag;
		fwrite(&tag, 4, 1, fd);
		fwrite(content->frames[i].payload, 4, length_of_frames[i], fd);
	}
	return;
}

void jfif_parse(struct jfif_container* content, uint32_t* buffer, uint32_t length){
	if (length != sizeof(struct jfif_container) / 4) {
		printf("length:%d\n", length);
		printf("FIXME! wrong jfif attachement size.\n");
		exit(1);
	}
	memcpy(content, buffer, sizeof(struct jfif_container));
	return;
}

void flac_parse(uint32_t* content, uint32_t length){
	return;
}
