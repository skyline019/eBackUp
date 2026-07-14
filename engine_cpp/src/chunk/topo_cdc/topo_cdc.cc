#include "ebbackup/chunk/topo_cdc.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "ebbackup/chunk/topo_cdc_internal.h"
#include "ebbackup/chunk/topo_chain_internal.h"
#include "ebbackup/chunk/topo_tri_internal.h"
#include "ebbackup/common/digest.h"

namespace ebbackup {

namespace {

Status CollectChunkCutsHom(const uint8_t* data, size_t len,
                           const TopoCdcConfig& cfg, const uint32_t gear[256],
                           std::vector<size_t>* offsets,
                           std::vector<uint32_t>* lengths) {
  offsets->clear();
  lengths->clear();
  if (len == 0) return Status::Ok();
  if (len <= cfg.min_size) {
    offsets->push_back(0);
    lengths->push_back(static_cast<uint32_t>(len));
    return Status::Ok();
  }

  uint32_t mask = 0;
  topo_cdc_internal::BuildTopoMasks(cfg.avg_size, cfg.topo_calib_permille,
                                    cfg.topo_shift, &mask);

  size_t pos = 0;
  while (pos < len) {
    const size_t remaining = len - pos;
    if (remaining <= cfg.min_size) {
      offsets->push_back(pos);
      lengths->push_back(static_cast<uint32_t>(remaining));
      break;
    }

    const size_t rel_scan = cfg.min_size;
    const size_t rel_limit = std::min<size_t>(cfg.max_size, remaining);
    size_t rel_cut = rel_limit;
    bool found = false;
    if (rel_scan < rel_limit) {
      topo_cdc_internal::ScanHomCut(data + pos, rel_scan, rel_limit,
                                    cfg.window_w, mask, gear, &rel_cut, &found);
    }

    size_t cut = 0;
    if (found) {
      cut = pos + rel_cut;
    } else if (remaining > cfg.max_size) {
      cut = pos + cfg.max_size;
    } else {
      cut = len;
    }

    offsets->push_back(pos);
    lengths->push_back(static_cast<uint32_t>(cut - pos));
    pos = cut;
  }
  return Status::Ok();
}

Status CollectChunkCutsChain(const uint8_t* data, size_t len,
                             const TopoCdcConfig& cfg,
                             std::vector<size_t>* offsets,
                             std::vector<uint32_t>* lengths) {
  offsets->clear();
  lengths->clear();
  if (len == 0) return Status::Ok();
  if (len <= cfg.min_size) {
    offsets->push_back(0);
    lengths->push_back(static_cast<uint32_t>(len));
    return Status::Ok();
  }

  size_t pos = 0;
  while (pos < len) {
    const size_t remaining = len - pos;
    if (remaining <= cfg.min_size) {
      offsets->push_back(pos);
      lengths->push_back(static_cast<uint32_t>(remaining));
      break;
    }

    const size_t rel_scan = cfg.min_size;
    const size_t rel_limit = std::min<size_t>(cfg.max_size, remaining);
    size_t rel_cut = rel_limit;
    bool found = false;
    if (rel_scan < rel_limit) {
      topo_chain_internal::ScanChainCut(data + pos, rel_scan, rel_limit, cfg,
                                        &rel_cut, &found);
    }

    size_t cut = 0;
    if (found) {
      cut = pos + rel_cut;
    } else if (remaining > cfg.max_size) {
      cut = pos + cfg.max_size;
    } else {
      cut = len;
    }

    offsets->push_back(pos);
    lengths->push_back(static_cast<uint32_t>(cut - pos));
    pos = cut;
  }
  return Status::Ok();
}

Status CollectChunkCutsTri(const uint8_t* data, size_t len,
                           const TopoCdcConfig& cfg, const uint32_t gear[256],
                           std::vector<size_t>* offsets,
                           std::vector<uint32_t>* lengths) {
  offsets->clear();
  lengths->clear();
  if (len == 0) return Status::Ok();
  if (len <= cfg.min_size) {
    offsets->push_back(0);
    lengths->push_back(static_cast<uint32_t>(len));
    return Status::Ok();
  }

  uint32_t mask = 0;
  topo_cdc_internal::BuildTopoMasks(cfg.avg_size, cfg.topo_calib_permille,
                                    cfg.topo_shift, &mask);

  size_t pos = 0;
  while (pos < len) {
    const size_t remaining = len - pos;
    if (remaining <= cfg.min_size) {
      offsets->push_back(pos);
      lengths->push_back(static_cast<uint32_t>(remaining));
      break;
    }

    const size_t rel_scan = cfg.min_size;
    const size_t rel_limit = std::min<size_t>(cfg.max_size, remaining);
    size_t rel_cut = rel_limit;
    bool found = false;
    if (rel_scan < rel_limit) {
      topo_tri_internal::ScanTriCut(data + pos, rel_scan, rel_limit, cfg, mask,
                                    gear, &rel_cut, &found);
    }

    size_t cut = 0;
    if (found) {
      cut = pos + rel_cut;
    } else if (remaining > cfg.max_size) {
      cut = pos + cfg.max_size;
    } else {
      cut = len;
    }

    offsets->push_back(pos);
    lengths->push_back(static_cast<uint32_t>(cut - pos));
    pos = cut;
  }
  return Status::Ok();
}

void HashChunkRegions(DigestAlgo algo, const uint8_t* data,
                      const std::vector<size_t>& offsets,
                      const std::vector<uint32_t>& lengths,
                      std::vector<ChunkDescriptor>* out) {
  for (size_t i = 0; i < offsets.size(); ++i) {
    ChunkDescriptor desc{};
    desc.offset = offsets[i];
    desc.length = lengths[i];
    ContentHash(algo, data + desc.offset, desc.length, desc.hash);
    out->push_back(desc);
  }
}

}  // namespace

bool CdcTopoCdcEnabled() {
  const char* env = std::getenv("EBBACKUP_CDC_ALGO");
  return env && std::strcmp(env, "topocdc") == 0;
}

bool CdcTopoChainEnabled() {
  const char* env = std::getenv("EBBACKUP_CDC_ALGO");
  return env && std::strcmp(env, "topochain") == 0;
}

bool CdcTopoTriVariantRequested() {
  const char* env = std::getenv("EBBACKUP_TOPO_VARIANT");
  return env && std::strcmp(env, "tri") == 0;
}

TopoCdcSlice::TopoCdcSlice(TopoCdcConfig config) : config_(config) {
  if (config_.variant == TopoCdcVariant::kHom ||
      config_.variant == TopoCdcVariant::kTri) {
    topo_cdc_internal::InitGearTable(gear_, config_.table_seed);
  }
}

Status TopoCdcSlice::ChunkCuts(const uint8_t* data, size_t len,
                               std::vector<size_t>* offsets,
                               std::vector<uint32_t>* lengths) const {
  if (!offsets || !lengths) return Status::InvalidArgument("null out");
  if (config_.variant == TopoCdcVariant::kChain) {
    return CollectChunkCutsChain(data, len, config_, offsets, lengths);
  }
  if (config_.variant == TopoCdcVariant::kTri) {
    return CollectChunkCutsTri(data, len, config_, gear_, offsets, lengths);
  }
  return CollectChunkCutsHom(data, len, config_, gear_, offsets, lengths);
}

Status TopoCdcSlice::Chunk(const uint8_t* data, size_t len,
                           std::vector<ChunkDescriptor>* out) const {
  if (!out) return Status::InvalidArgument("out is null");
  out->clear();
  if (len == 0) return Status::Ok();

  std::vector<size_t> offsets;
  std::vector<uint32_t> lengths;
  const Status cuts_st = ChunkCuts(data, len, &offsets, &lengths);
  if (!cuts_st.ok()) return cuts_st;

  if (len <= config_.min_size) {
    ChunkDescriptor desc{};
    desc.offset = 0;
    desc.length = static_cast<uint32_t>(len);
    ContentHash(config_.digest_algo, data, len, desc.hash);
    out->push_back(desc);
    return Status::Ok();
  }

  HashChunkRegions(config_.digest_algo, data, offsets, lengths, out);
  return Status::Ok();
}

}  // namespace ebbackup
