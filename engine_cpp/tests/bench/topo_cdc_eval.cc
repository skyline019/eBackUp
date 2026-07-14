#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/fast_cdc.h"
#include "ebbackup/chunk/gt_cdc.h"
#include "ebbackup/chunk/gt_cdc_internal.h"
#include "ebbackup/chunk/topo_cdc.h"
#include "ebbackup/chunk/topo_cdc_internal.h"
#include "ebbackup/chunk/topo_chain_internal.h"
#include "ebbackup/chunk/topo_ph.h"
#include "ebbackup/chunk/topo_ph_internal.h"
#include "ebbackup/chunk/topo_phn.h"
#include "ebbackup/chunk/topo_phn_internal.h"
#include "test_util.h"

namespace {

constexpr size_t kCalibSize = 1024 * 1024;

double MeanLen(const std::vector<uint32_t>& lengths) {
  if (lengths.empty()) return 0.0;
  double s = 0.0;
  for (uint32_t l : lengths) s += static_cast<double>(l);
  return s / static_cast<double>(lengths.size());
}

uint16_t CalibrateTopo(uint32_t seed) {
  ebbackup::TopoCdcConfig cfg =
      ebbackup::TopoCdcConfigForProfile(ebbackup::ChunkProfileMode::kDefault);
  cfg.table_seed = seed;
  std::vector<uint8_t> sample(kCalibSize);
  ebbackup::topo_cdc_internal::FillTopoCalibSample(sample.data(), sample.size(),
                                                    seed);
  return ebbackup::topo_cdc_internal::CalibrateTopoPermille(
      sample.data(), sample.size(), cfg, seed);
}

uint16_t CalibrateChain(uint32_t seed) {
  ebbackup::TopoCdcConfig cfg =
      ebbackup::TopoCdcConfigForProfile(ebbackup::ChunkProfileMode::kDefault);
  cfg.variant = ebbackup::TopoCdcVariant::kChain;
  cfg.chain_lfsr_seed = seed;
  cfg.chain_enable_beta1 = true;
  std::vector<uint8_t> sample(kCalibSize);
  ebbackup::topo_chain_internal::FillChainCalibSample(sample.data(), sample.size(),
                                                      seed);
  return ebbackup::topo_chain_internal::CalibrateChainStrideLog(
      sample.data(), sample.size(), cfg);
}

uint16_t CalibratePh(uint32_t seed, ebbackup::TopoPhKernel kernel) {
  ebbackup::TopoPhConfig cfg =
      ebbackup::TopoPhConfigForProfile(ebbackup::ChunkProfileMode::kDefault);
  cfg.table_seed = seed;
  cfg.kernel = kernel;
  std::vector<uint8_t> sample(kCalibSize);
  ebbackup::topo_ph_internal::FillTopoPhCalibSample(sample.data(), sample.size(),
                                                    seed);
  return ebbackup::topo_ph_internal::CalibrateTopoPhPermille(
      sample.data(), sample.size(), cfg, seed);
}

ebbackup::TopoPhnConfig MakePhnCfg(uint32_t seed, ebbackup::TopoPhnKernel kernel) {
  ebbackup::TopoPhnConfig cfg =
      ebbackup::TopoPhnConfigForProfile(ebbackup::ChunkProfileMode::kDefault);
  cfg.table_seed = seed;
  cfg.kernel = kernel;
  cfg.k_points =
      kernel == ebbackup::TopoPhnKernel::kPhH0Native ? 16 : 8;
  std::vector<uint8_t> sample(ebbackup::topo_phn_internal::kPhnCalibSampleBytes);
  ebbackup::topo_phn_internal::FillPhnCalibSample(sample.data(), sample.size(),
                                                   seed);
  ebbackup::topo_phn_internal::CalibratePhnCutParams(sample.data(), sample.size(),
                                                     &cfg);
  return cfg;
}

void RunFamily(const uint8_t* data, size_t len, const char* name,
               uint16_t topo_calib_permille, uint16_t chain_stride_log,
               uint16_t tri_v2_calib, uint16_t ph_h0_calib,
               const ebbackup::TopoPhnConfig& tri_native_cfg,
               const ebbackup::TopoPhnConfig& ph_native_cfg,
               std::vector<size_t>* offsets, std::vector<uint32_t>* lengths) {
  offsets->clear();
  lengths->clear();
  if (std::strcmp(name, "stream") == 0) {
    ebbackup::FastCdcSlice slice;
    (void)slice.ChunkCuts(data, len, offsets, lengths);
  } else if (std::strcmp(name, "gtcdc") == 0) {
    ebbackup::GtCdcConfig cfg = ebbackup::GtCdcConfigForFileSize(
        len, ebbackup::ChunkProfileMode::kAuto, ebbackup::DigestAlgo::kLegacy,
        ebbackup::GtCdcKernel::kTwoFGear);
    cfg.table_seed = 0x12345678u;
    cfg.nc_level = 2;
    ebbackup::gtcdc_internal::InitGearTableForConfig(&cfg);
    ebbackup::GtCdcSlice slice(cfg);
    (void)slice.ChunkCuts(data, len, offsets, lengths);
  } else if (std::strcmp(name, "chain") == 0) {
    ebbackup::TopoCdcConfig cfg = ebbackup::TopoCdcConfigForFileSize(
        len, ebbackup::ChunkProfileMode::kAuto, ebbackup::DigestAlgo::kLegacy);
    cfg.variant = ebbackup::TopoCdcVariant::kChain;
    cfg.chain_lfsr_seed = 0x12345678u;
    cfg.chain_stride_log = static_cast<uint8_t>(chain_stride_log & 0xFFu);
    cfg.chain_enable_beta1 = true;
    ebbackup::TopoCdcSlice slice(cfg);
    (void)slice.ChunkCuts(data, len, offsets, lengths);
  } else if (std::strcmp(name, "tri") == 0) {
    ebbackup::TopoCdcConfig cfg = ebbackup::TopoCdcConfigForFileSize(
        len, ebbackup::ChunkProfileMode::kAuto, ebbackup::DigestAlgo::kLegacy);
    cfg.variant = ebbackup::TopoCdcVariant::kTri;
    cfg.table_seed = 0x12345678u;
    cfg.topo_calib_permille = topo_calib_permille;
    ebbackup::TopoCdcSlice slice(cfg);
    (void)slice.ChunkCuts(data, len, offsets, lengths);
  } else if (std::strcmp(name, "tri_v2") == 0) {
    ebbackup::TopoPhConfig cfg = ebbackup::TopoPhConfigForFileSize(
        len, ebbackup::ChunkProfileMode::kAuto, ebbackup::DigestAlgo::kLegacy);
    cfg.kernel = ebbackup::TopoPhKernel::kTriV2;
    cfg.table_seed = 0x12345678u;
    cfg.topo_calib_permille = tri_v2_calib;
    ebbackup::TopoPhSlice slice(cfg);
    (void)slice.ChunkCuts(data, len, offsets, lengths);
  } else if (std::strcmp(name, "ph_h0") == 0) {
    ebbackup::TopoPhConfig cfg = ebbackup::TopoPhConfigForFileSize(
        len, ebbackup::ChunkProfileMode::kAuto, ebbackup::DigestAlgo::kLegacy);
    cfg.kernel = ebbackup::TopoPhKernel::kPhH0;
    cfg.table_seed = 0x12345678u;
    cfg.topo_calib_permille = ph_h0_calib;
    ebbackup::TopoPhSlice slice(cfg);
    (void)slice.ChunkCuts(data, len, offsets, lengths);
  } else if (std::strcmp(name, "tri_native") == 0) {
    ebbackup::TopoPhnSlice slice(tri_native_cfg);
    (void)slice.ChunkCuts(data, len, offsets, lengths);
  } else if (std::strcmp(name, "ph_native") == 0) {
    ebbackup::TopoPhnSlice slice(ph_native_cfg);
    (void)slice.ChunkCuts(data, len, offsets, lengths);
  } else {
    ebbackup::TopoCdcConfig cfg = ebbackup::TopoCdcConfigForFileSize(
        len, ebbackup::ChunkProfileMode::kAuto, ebbackup::DigestAlgo::kLegacy);
    cfg.table_seed = 0x12345678u;
    cfg.topo_calib_permille = topo_calib_permille;
    ebbackup::TopoCdcSlice slice(cfg);
    (void)slice.ChunkCuts(data, len, offsets, lengths);
  }
}

double Reuse1Byte(const uint8_t* data, size_t len, size_t delta,
                  const std::vector<size_t>& base_off, const char* name,
                  uint16_t topo_calib_permille, uint16_t chain_stride_log,
                  uint16_t tri_v2_calib, uint16_t ph_h0_calib,
                  const ebbackup::TopoPhnConfig& tri_native_cfg,
                  const ebbackup::TopoPhnConfig& ph_native_cfg) {
  if (base_off.empty() || delta >= len) return 0.0;
  std::vector<uint8_t> mutated(len + 1);
  std::memcpy(mutated.data(), data, delta);
  mutated[delta] = 0xA5;
  std::memcpy(mutated.data() + delta + 1, data + delta, len - delta);
  std::vector<size_t> off2;
  std::vector<uint32_t> len2;
  RunFamily(mutated.data(), mutated.size(), name, topo_calib_permille,
            chain_stride_log, tri_v2_calib, ph_h0_calib, tri_native_cfg,
            ph_native_cfg, &off2, &len2);
  size_t hits = 0;
  for (size_t b : base_off) {
    const size_t expect = b < delta ? b : b + 1;
    for (size_t o : off2) {
      if (o == expect) {
        ++hits;
        break;
      }
    }
  }
  return 100.0 * static_cast<double>(hits) /
         static_cast<double>(base_off.size());
}

}  // namespace

int main(int argc, char** argv) {
  bool quick = false;
  std::string json_out;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--quick") == 0) {
      quick = true;
    } else if (std::strcmp(argv[i], "--json-out") == 0 && i + 1 < argc) {
      json_out = argv[++i];
    }
  }

  constexpr size_t kSize = 8 * 1024 * 1024;
  constexpr size_t kInsertAt = 5 * 1024 * 1024;
  const uint32_t seed = 0x12345678u;
  const uint16_t topo_calib_permille = CalibrateTopo(seed);
  const uint16_t chain_stride_log = CalibrateChain(seed);
  const uint16_t tri_v2_calib =
      CalibratePh(seed, ebbackup::TopoPhKernel::kTriV2);
  const uint16_t ph_h0_calib =
      CalibratePh(seed, ebbackup::TopoPhKernel::kPhH0);
  const ebbackup::TopoPhnConfig tri_native_cfg =
      MakePhnCfg(seed, ebbackup::TopoPhnKernel::kTriNative);
  const ebbackup::TopoPhnConfig ph_native_cfg =
      MakePhnCfg(seed, ebbackup::TopoPhnKernel::kPhH0Native);
  const std::string data = ebbackup::test::MakeRandomData(kSize, 93);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());

  const char* families[] = {"stream",     "gtcdc",      "topo",
                            "tri",        "chain",      "tri_v2",
                            "ph_h0",      "tri_native", "ph_native"};
  const size_t family_count = quick ? 1 : 9;

  std::string json = "{\n";
  json += "  \"bytes\": " + std::to_string(kSize) + ",\n";
  json += "  \"topo_calib_permille\": " + std::to_string(topo_calib_permille) + ",\n";
  json += "  \"chain_stride_log\": " + std::to_string(chain_stride_log) + ",\n";
  json += "  \"tri_v2_calib\": " + std::to_string(tri_v2_calib) + ",\n";
  json += "  \"ph_h0_calib\": " + std::to_string(ph_h0_calib) + ",\n";
  json += "  \"tri_native_stride\": " +
          std::to_string(tri_native_cfg.event_stride) + ",\n";
  json += "  \"ph_native_stride\": " +
          std::to_string(ph_native_cfg.event_stride) + ",\n";
  json += "  \"families\": {\n";

  std::vector<size_t> tri_off;
  std::vector<size_t> ph_off;
  double tri_reuse = 0.0;
  double ph_mean = 0.0;
  double ph_scan_ms = 0.0;

  for (size_t fi = 0; fi < family_count; ++fi) {
    std::vector<size_t> off;
    std::vector<uint32_t> len;
    const auto t0 = std::chrono::steady_clock::now();
    RunFamily(bytes, kSize, families[fi], topo_calib_permille, chain_stride_log,
              tri_v2_calib, ph_h0_calib, tri_native_cfg, ph_native_cfg, &off,
              &len);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double mean = MeanLen(len);
    const double reuse =
        Reuse1Byte(bytes, kSize, kInsertAt, off, families[fi],
                   topo_calib_permille, chain_stride_log, tri_v2_calib,
                   ph_h0_calib, tri_native_cfg, ph_native_cfg);
    if (std::strcmp(families[fi], "tri_native") == 0) {
      tri_off = off;
      tri_reuse = reuse;
    }
    if (std::strcmp(families[fi], "ph_native") == 0) {
      ph_off = off;
      ph_mean = mean;
      ph_scan_ms = ms;
    }
    json += "    \"" + std::string(families[fi]) + "\": {";
    json += "\"chunks\": " + std::to_string(len.size()) + ", ";
    json += "\"mean_chunk\": " + std::to_string(mean) + ", ";
    json += "\"scan_ms\": " + std::to_string(ms) + ", ";
    json += "\"reuse_1byte_pct\": " + std::to_string(reuse) + "}";
    json += (fi + 1 < family_count ? ",\n" : "\n");
  }
  json += "  }\n}\n";

  std::printf("%s", json.c_str());

  if (!quick && tri_off == ph_off) {
    std::fprintf(stderr,
                 "FAIL: tri_native and ph_native cuts are identical "
                 "(AND-gate not differentiating)\n");
    return 1;
  }
  if (!quick && tri_reuse + 1e-9 < 80.0) {
    std::fprintf(stderr,
                 "FAIL: tri_native reuse_1byte_pct %.1f below 80\n", tri_reuse);
    return 1;
  }
  if (!quick && ph_mean > ph_native_cfg.avg_size * 1.60) {
    std::fprintf(stderr,
                 "FAIL: ph_native mean_chunk %.0f exceeds avg*1.6 (%.0f)\n",
                 ph_mean, ph_native_cfg.avg_size * 1.60);
    return 1;
  }
  if (!quick && ph_scan_ms > 300.0) {
    std::fprintf(stderr,
                 "FAIL: ph_native scan_ms %.1f exceeds 300ms budget\n",
                 ph_scan_ms);
    return 1;
  }

  if (!json_out.empty()) {
    std::ofstream out(json_out, std::ios::binary);
    if (!out) {
      std::fprintf(stderr, "failed to write %s\n", json_out.c_str());
      return 1;
    }
    out << json;
  }
  return 0;
}
