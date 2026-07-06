#include "ebbackup/chunk/eb_hcrbo.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "ebbackup/chunk/rabin_anchor.h"
#include "ebbackup/chunk/rolling_checksum.h"
#include "ebbackup/common/digest.h"

namespace ebbackup {

namespace {

struct CfiAnchorIndex {
  std::vector<size_t> by_offset;
  std::unordered_map<uint32_t, std::vector<size_t>> by_rolling;
  uint64_t rolling_skip_hits{0};
};

void BuildCfiAnchorIndex(const CfiIndex& history, CfiAnchorIndex* index) {
  if (!index) return;
  index->by_offset.resize(history.anchors.size());
  for (size_t i = 0; i < history.anchors.size(); ++i) {
    index->by_offset[i] = i;
  }
  std::sort(index->by_offset.begin(), index->by_offset.end(),
            [&](size_t a, size_t b) {
              return history.anchors[a].offset < history.anchors[b].offset;
            });
  for (size_t i = 0; i < history.anchors.size(); ++i) {
    const uint32_t rc = history.anchors[i].rolling_checksum;
    if (rc != 0) {
      index->by_rolling[rc].push_back(i);
    }
  }
}

size_t LowerBoundAnchorPos(const CfiAnchorIndex& index,
                           const CfiIndex& history, size_t pos) {
  const auto it = std::lower_bound(
      index.by_offset.begin(), index.by_offset.end(), pos,
      [&](size_t anchor_idx, size_t offset) {
        return history.anchors[anchor_idx].offset < offset;
      });
  return static_cast<size_t>(it - index.by_offset.begin());
}

}  // namespace

EbHcrboChunker::EbHcrboChunker(EbHcrboConfig config)
    : config_(config), fast_(config.fast) {}

Status EbHcrboChunker::ChunkRegion(const uint8_t* data, size_t len,
                                   size_t region_start, size_t region_end,
                                   bool use_rabin_edges,
                                   std::vector<ChunkDescriptor>* out,
                                   CfiIndex* cfi_out,
                                   EbHcrboStats* stats) const {
  if (region_end <= region_start) return Status::Ok();
  const size_t region_len = region_end - region_start;
  if (region_len <= config_.fast.min_size) {
    ChunkDescriptor desc{};
    desc.offset = region_start;
    desc.length = static_cast<uint32_t>(region_len);
    Sha256(data + region_start, region_len, desc.hash);
    out->push_back(desc);
    if (cfi_out) {
      ChunkAnchor a{};
      a.offset = region_start;
      a.length = desc.length;
      std::memcpy(a.hash, desc.hash, 32);
      a.strength = use_rabin_edges ? AnchorStrength::kStrong
                                   : AnchorStrength::kWeak;
      cfi_out->anchors.push_back(a);
    }
    return Status::Ok();
  }

  size_t inner_start = region_start;
  size_t inner_end = region_end;
  if (use_rabin_edges) {
    const size_t edge = std::min<size_t>(config_.strong_anchor_bytes,
                                         region_len / 4);
    size_t left_cut = region_start + edge;
    if (RabinFindAnchor(data, len, region_start, left_cut, 64,
                        config_.rabin_mask, &left_cut)) {
      ChunkDescriptor desc{};
      desc.offset = region_start;
      desc.length = static_cast<uint32_t>(left_cut - region_start);
      Sha256(data + desc.offset, desc.length, desc.hash);
      out->push_back(desc);
      if (stats) ++stats->chunks_cut_rabin;
      if (cfi_out) {
        ChunkAnchor a{};
        a.offset = desc.offset;
        a.length = desc.length;
        std::memcpy(a.hash, desc.hash, 32);
        a.strength = AnchorStrength::kStrong;
        cfi_out->anchors.push_back(a);
      }
      inner_start = left_cut;
    }
    if (inner_end > inner_start + config_.fast.min_size) {
      size_t rc = inner_end;
      if (RabinFindAnchor(data, len, inner_end - edge, inner_end, 64,
                          config_.rabin_mask, &rc)) {
        inner_end = rc;
      }
    }
  }

  if (inner_end > inner_start) {
    std::vector<ChunkDescriptor> inner;
    const Status st =
        fast_.Chunk(data + inner_start, inner_end - inner_start, &inner);
    if (!st.ok()) return st;
    for (auto& d : inner) {
      d.offset += inner_start;
      out->push_back(d);
      if (stats) ++stats->chunks_cut_fastcdc;
      if (cfi_out) {
        ChunkAnchor a{};
        a.offset = d.offset;
        a.length = d.length;
        std::memcpy(a.hash, d.hash, 32);
        a.strength = AnchorStrength::kWeak;
        cfi_out->anchors.push_back(a);
      }
    }
  }

  if (use_rabin_edges && inner_end < region_end) {
    ChunkDescriptor desc{};
    desc.offset = inner_end;
    desc.length = static_cast<uint32_t>(region_end - inner_end);
    Sha256(data + desc.offset, desc.length, desc.hash);
    out->push_back(desc);
    if (stats) ++stats->chunks_cut_rabin;
    if (cfi_out) {
      ChunkAnchor a{};
      a.offset = desc.offset;
      a.length = desc.length;
      std::memcpy(a.hash, desc.hash, 32);
      a.strength = AnchorStrength::kStrong;
      cfi_out->anchors.push_back(a);
    }
  }
  return Status::Ok();
}

Status EbHcrboChunker::ChunkFull(const uint8_t* data, size_t len,
                                 std::vector<ChunkDescriptor>* out,
                                 CfiIndex* cfi_out,
                                 EbHcrboStats* stats) const {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  if (cfi_out) cfi_out->anchors.clear();
  return ChunkRegion(data, len, 0, len, false, out, cfi_out, stats);
}

Status EbHcrboChunker::ChunkIncremental(const uint8_t* data, size_t len,
                                        const CfiIndex& history,
                                        std::vector<ChunkDescriptor>* out,
                                        CfiIndex* cfi_out,
                                        EbHcrboStats* stats) const {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  if (cfi_out) cfi_out->anchors.clear();
  if (history.anchors.empty()) {
    return ChunkFull(data, len, out, cfi_out, stats);
  }

  CfiAnchorIndex index;
  BuildCfiAnchorIndex(history, &index);
  size_t sorted_pos = 0;
  size_t pos = 0;
  while (pos < len && sorted_pos < index.by_offset.size()) {
    const size_t anchor_idx = index.by_offset[sorted_pos];
    const ChunkAnchor& anchor = history.anchors[anchor_idx];
    if (anchor.offset > pos) {
      const Status st = ChunkRegion(data, len, pos, anchor.offset, false, out,
                                    cfi_out, stats);
      if (!st.ok()) return st;
      pos = anchor.offset;
      continue;
    }
    if (anchor.offset < pos) {
      ++sorted_pos;
      continue;
    }
    if (anchor.offset + anchor.length > len) {
      const Status st =
          ChunkRegion(data, len, pos, len, false, out, cfi_out, stats);
      return st;
    }
    bool hash_match = false;
    if (anchor.rolling_checksum != 0) {
      const uint32_t rc = RollingChecksum(data + anchor.offset, anchor.length);
      if (rc == anchor.rolling_checksum) {
        uint8_t current_hash[32];
        Sha256(data + anchor.offset, anchor.length, current_hash);
        hash_match = std::memcmp(current_hash, anchor.hash, 32) == 0;
      } else {
        const auto it = index.by_rolling.find(anchor.rolling_checksum);
        if (it != index.by_rolling.end()) {
          index.rolling_skip_hits += it->second.size();
        }
      }
    } else {
      uint8_t current_hash[32];
      Sha256(data + anchor.offset, anchor.length, current_hash);
      hash_match = std::memcmp(current_hash, anchor.hash, 32) == 0;
    }
    if (hash_match) {
      ChunkDescriptor desc{};
      desc.offset = anchor.offset;
      desc.length = anchor.length;
      std::memcpy(desc.hash, anchor.hash, 32);
      desc.reused_from_cfi = true;
      out->push_back(desc);
      if (stats) ++stats->chunks_reused_from_cfi;
      if (cfi_out) cfi_out->anchors.push_back(anchor);
      pos = anchor.offset + anchor.length;
      ++sorted_pos;
      continue;
    }

    const size_t change_end =
        (sorted_pos + 1 < index.by_offset.size())
            ? history.anchors[index.by_offset[sorted_pos + 1]].offset
            : len;
    const Status st = ChunkRegion(data, len, pos, change_end, false, out,
                                  cfi_out, stats);
    if (!st.ok()) return st;
    pos = change_end;
    sorted_pos = LowerBoundAnchorPos(index, history, pos);
  }
  if (pos < len) {
    const Status st = ChunkRegion(data, len, pos, len, false, out, cfi_out, stats);
    if (!st.ok()) return st;
  }
  if (stats) stats->cfi_rolling_skip_hits = index.rolling_skip_hits;
  return Status::Ok();
}

}  // namespace ebbackup
