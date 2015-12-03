// Copyright (c) 2015 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
#include "webm2pes.h"

namespace {
void Usage(const char* argv[]) {
  printf("Usage: %s <WebM file> <output file>", argv[0]);
}

bool WriteUint8(std::uint8_t val, std::FILE* fileptr) {
  if (fileptr == nullptr)
    return false;
  return (std::fputc(val, fileptr) == val);
}

std::int64_t NanosecondsTo90KhzTicks(std::int64_t nanoseconds) {
  const double kNanosecondsPerSecond = 1000000000.0;
  const double pts_seconds = nanoseconds / kNanosecondsPerSecond;
  return pts_seconds * 90000;
}
}  // namespace

namespace libwebm {

//
// PesOptionalHeader methods.
//

void PesOptionalHeader::SetPtsBits(std::int64_t pts_90khz) {
  std::uint64_t* pts_bits = &pts.bits;
  *pts_bits = 0;

  // PTS is broken up and stored in 40 bits as shown:
  //
  //  PES PTS Only flag
  // /                  Marker              Marker              Marker
  // |                 /                   /                   /
  // |                 |                   |                   |
  // 7654  321         0  765432107654321  0  765432107654321  0
  // 0010  PTS 32-30   1  PTS 29-15        1  PTS 14-0         1
  const std::uint32_t pts1 = (pts_90khz >> 30) & 0x7;
  const std::uint32_t pts2 = (pts_90khz >> 15) & 0x7FFF;
  const std::uint32_t pts3 = pts_90khz & 0x7FFF;

  std::uint8_t buffer[5] = {0};
  // PTS only flag.
  buffer[0] |= 1 << 5;
  // Top 3 bits of PTS and 1 bit marker.
  buffer[0] |= pts1 << 1;
  // Marker.
  buffer[0] |= 1;

  // Next 15 bits of pts and 1 bit marker.
  // Top 8 bits of second PTS chunk.
  buffer[1] |= (pts2 >> 7) & 0xff;
  // bottom 7 bits of second PTS chunk.
  buffer[2] |= (pts2 << 1);
  // Marker.
  buffer[2] |= 1;

  // Last 15 bits of pts and 1 bit marker.
  // Top 8 bits of second PTS chunk.
  buffer[3] |= (pts3 >> 7) & 0xff;
  // bottom 7 bits of second PTS chunk.
  buffer[4] |= (pts3 << 1);
  // Marker.
  buffer[4] |= 1;

  // Write bits into PesHeaderField.
  std::memcpy(reinterpret_cast<std::uint8_t*>(pts_bits), buffer, 5);
}

// Writes fields to |file| and returns true. Returns false when write or
// field value validation fails.
bool PesOptionalHeader::Write(std::FILE* file, bool write_pts) const {
  if (file == nullptr) {
    std::fprintf(stderr, "Webm2Pes: nullptr in opt header writer.\n");
    return false;
  }

  std::uint8_t header[9] = {0};
  std::uint8_t* byte = header;

  if (marker.Check() != true || scrambling.Check() != true ||
      priority.Check() != true || data_alignment.Check() != true ||
      copyright.Check() != true || original.Check() != true ||
      has_pts.Check() != true || has_dts.Check() != true ||
      pts.Check() != true || stuffing_byte.Check() != true) {
    std::fprintf(stderr, "Webm2Pes: Invalid PES Optional Header field.\n");
    return false;
  }

  // TODO(tomfinegan): As noted in above, the PesHeaderFields should be an
  // array (or some data structure) that can be iterated over.

  // First byte of header, fields: marker, scrambling, priority, alignment,
  // copyright, original.
  *byte = 0;
  *byte |= marker.bits << marker.shift;
  *byte |= scrambling.bits << scrambling.shift;
  *byte |= priority.bits << priority.shift;
  *byte |= data_alignment.bits << data_alignment.shift;
  *byte |= copyright.bits << copyright.shift;
  *byte |= original.bits << original.shift;

  // Second byte of header, fields: has_pts, has_dts, unused fields.
  *++byte = 0;
  if (write_pts == true) {
    *byte |= has_pts.bits << has_pts.shift;
    *byte |= has_dts.bits << has_dts.shift;
  }

  // Third byte of header, fields: remaining size of header.
  *++byte = remaining_size.bits;  // Field is 8 bits wide.

  int num_stuffing_bytes = (pts.num_bits + 7 / 8) + 1;
  if (write_pts == true) {
    // Set the PTS value and adjust stuffing byte count accordingly.
    *++byte = (pts.bits >> 32) & 0xff;
    *++byte = (pts.bits >> 24) & 0xff;
    *++byte = (pts.bits >> 16) & 0xff;
    *++byte = (pts.bits >> 8) & 0xff;
    *++byte = pts.bits & 0xff;
    num_stuffing_bytes = 1;
  }

  // Add the stuffing byte(s).
  for (int i = 0; i < num_stuffing_bytes; ++i)
    *++byte = stuffing_byte.bits;

  if (std::fwrite(reinterpret_cast<void*>(header), 1, size_in_bytes(), file) !=
      size_in_bytes()) {
    std::fprintf(stderr, "Webm2Pes: unable to write PES opt header to file.\n");
    return false;
  }

  return true;
}

//
// BCMVHeader methods.
//

bool BCMVHeader::Write(std::FILE* fileptr) const {
  if (fileptr == nullptr) {
    std::fprintf(stderr, "Webm2Pes: nullptr for file in BCMV Write.\n");
    return false;
  }
  if (std::fwrite(bcmv, 1, 4, fileptr) != 4) {
    std::fprintf(stderr, "Webm2Pes: BCMV write failed.\n");
  }
  const std::size_t kRemainingBytes = 6;
  const uint8_t buffer[kRemainingBytes] = {(length >> 24) & 0xff,
                                           (length >> 16) & 0xff,
                                           (length >> 8) & 0xff,
                                           length & 0xff,
                                           0,
                                           0 /* 2 bytes 0 padding */};
  for (std::int8_t i = 0; i < kRemainingBytes; ++i) {
    if (WriteUint8(buffer[i], fileptr) != true) {
      std::fprintf(stderr, "Webm2Pes: BCMV remainder write failed.\n");
      return false;
    }
  }
  return true;
}

//
// PesHeader methods.
//

// Writes out the header to |file|. Calls PesOptionalHeader::Write() to write
// |optional_header| contents. Returns true when successful, false otherwise.
bool PesHeader::Write(std::FILE* file, bool write_pts) const {
  if (file == nullptr) {
    std::fprintf(stderr, "Webm2Pes: nullptr in header writer.\n");
    return false;
  }

  // Write |start_code|.
  if (std::fwrite(reinterpret_cast<const void*>(start_code), 1, 4, file) != 4) {
    std::fprintf(stderr, "Webm2Pes: cannot write packet start code.\n");
    return false;
  }

  // Write |packet_length| as big endian.
  std::uint8_t byte = (packet_length >> 8) & 0xff;
  if (WriteUint8(byte, file) != true) {
    std::fprintf(stderr, "Webm2Pes: cannot write packet length (byte 0).\n");
    return false;
  }
  byte = packet_length & 0xff;
  if (WriteUint8(byte, file) != true) {
    std::fprintf(stderr, "Webm2Pes: cannot write packet length (byte 1).\n");
    return false;
  }

  // Write the (not really) optional header.
  if (optional_header.Write(file, write_pts) != true) {
    std::fprintf(stderr, "Webm2Pes: PES optional header write failed.");
    return false;
  }
  return true;
}

//
// Webm2Pes methods.
//

bool Webm2Pes::Convert() {
  if (input_file_name_.empty() || output_file_name_.empty()) {
    std::fprintf(stderr, "Webm2Pes: input and/or output file name(s) empty.\n");
    return false;
  }

  if (webm_reader_.Open(input_file_name_.c_str()) != 0) {
    std::fprintf(stderr, "Webm2Pes: Cannot open %s as input.\n",
                 input_file_name_.c_str());
    return false;
  }

  output_file_ = FilePtr(fopen(output_file_name_.c_str(), "wb"), FILEDeleter());
  if (output_file_ == nullptr) {
    std::fprintf(stderr, "Webm2Pes: Cannot open %s for output.\n",
                 output_file_name_.c_str());
    return false;
  }

  using mkvparser::Segment;
  Segment* webm_parser = nullptr;
  if (Segment::CreateInstance(&webm_reader_, 0 /* pos */,
                              webm_parser /* Segment*& */) != 0) {
    std::fprintf(stderr, "Webm2Pes: Cannot create WebM parser.\n");
    return false;
  }
  webm_parser_.reset(webm_parser);

  if (webm_parser_->Load() != 0) {
    std::fprintf(stderr, "Webm2Pes: Cannot parse %s.\n",
                 input_file_name_.c_str());
    return false;
  }

  // Store timecode scale.
  timecode_scale_ = webm_parser_->GetInfo()->GetTimeCodeScale();

  // Make sure there's a video track.
  const mkvparser::Tracks* tracks = webm_parser_->GetTracks();
  if (tracks == nullptr) {
    std::fprintf(stderr, "Webm2Pes: %s has no tracks.\n",
                 input_file_name_.c_str());
    return false;
  }
  for (int track_index = 0; track_index < tracks->GetTracksCount();
       ++track_index) {
    const mkvparser::Track* track = tracks->GetTrackByIndex(track_index);
    if (track && track->GetType() == mkvparser::Track::kVideo) {
      video_track_num_ = track_index + 1;
      break;
    }
  }
  if (video_track_num_ < 1) {
    std::fprintf(stderr, "Webm2Pes: No video track found in %s.\n",
                 input_file_name_.c_str());
    return false;
  }

  // Walk clusters in segment.
  const mkvparser::Cluster* cluster = webm_parser_->GetFirst();
  while (cluster != nullptr && cluster->EOS() == false) {
    const mkvparser::BlockEntry* block_entry = nullptr;
    std::int64_t block_status = cluster->GetFirst(block_entry);
    if (block_status < 0) {
      std::fprintf(stderr, "Webm2Pes: Cannot parse first block in %s.\n",
                   input_file_name_.c_str());
      return false;
    }

    // Walk blocks in cluster.
    while (block_entry != nullptr && block_entry->EOS() == false) {
      const mkvparser::Block* block = block_entry->GetBlock();
      if (block->GetTrackNumber() == video_track_num_) {
        const int frame_count = block->GetFrameCount();

        // Walk frames in block.
        for (int frame_num = 0; frame_num < frame_count; ++frame_num) {
          const mkvparser::Block::Frame& frame = block->GetFrame(frame_num);

          // Write frame out as PES packet(s).
          const bool pes_status =
              WritePesPacket(frame, block->GetTime(cluster));
          if (pes_status != true) {
            std::fprintf(stderr, "Webm2Pes: WritePesPacket failed.\n");
            return false;
          }
        }
      }
      block_status = cluster->GetNext(block_entry, block_entry);
      if (block_status < 0) {
        std::fprintf(stderr, "Webm2Pes: Cannot parse block in %s.\n",
                     input_file_name_.c_str());
        return false;
      }
    }

    cluster = webm_parser_->GetNext(cluster);
  }

  return true;
}

bool Webm2Pes::WritePesPacket(const mkvparser::Block::Frame& vpx_frame,
                              double nanosecond_pts) {
  bool packetize = false;
  PesHeader header;

  // TODO(tomfinegan): The length field in PES is actually number of bytes that
  // follow the length field, and does not include the 6 byte fixed portion of
  // the header (4 byte start code + 2 bytes for the length). We can fit in 6
  // more bytes if we really want to, and avoid packetization when size is very
  // close to UINT16_MAX.
  if (header.size() + vpx_frame.len > UINT16_MAX) {
    packetize = true;
  }

  const std::int64_t khz90_pts = NanosecondsTo90KhzTicks(nanosecond_pts);
  header.optional_header.SetPtsBits(khz90_pts);

  if (packetize == false) {
    header.packet_length =
        header.optional_header.size_in_bytes() + vpx_frame.len;
    if (header.Write(output_file_.get(), true) != true) {
      std::fprintf(stderr, "Webm2Pes: cannot write PES packet header.\n");
      return false;
    }

    // Write the BCMV Header.
    BCMVHeader bcmv_header(vpx_frame.len);
    if (bcmv_header.Write(output_file_.get()) != true) {
      std::fprintf(stderr, "Webm2Pes: BCMV write failed.\n");
      return false;
    }

    // Write frame.
    std::unique_ptr<uint8_t[]> frame_data(new (std::nothrow)
                                              uint8_t[vpx_frame.len]);
    if (frame_data.get() == nullptr) {
      std::fprintf(stderr, "Webm2Pes: Out of memory.\n");
      return false;
    }
    if (vpx_frame.Read(&webm_reader_, frame_data.get()) != 0) {
      std::fprintf(stderr, "Webm2Pes: Error reading VPx frame!\n");
      return false;
    }
    if (std::fwrite(frame_data.get(), 1, vpx_frame.len, output_file_.get()) !=
        vpx_frame.len) {
      std::fprintf(stderr, "Webm2Pes: VPx frame write failed.\n");
      return false;
    }

  } else {
    std::fprintf(
        stderr,
        "Webm2Pes: Packetization of large frames not implemented yet.\n");
    return false;
  }

  return true;
}

}  // namespace libwebm

int main(int argc, const char* argv[]) {
  if (argc < 3) {
    Usage(argv);
    return EXIT_FAILURE;
  }

  const std::string input_path = argv[1];
  const std::string output_path = argv[2];

  libwebm::Webm2Pes converter(input_path, output_path);
  return converter.Convert() == true ? EXIT_SUCCESS : EXIT_FAILURE;
}