#include "ebbackup/chunk/gt_cdc_view.h"
#include "ebbackup/chunk/gt_cdc_internal.h"

namespace ebbackup {
namespace gtcdc_internal {

bool GtCdcScanGear(const uint8_t* data, size_t scan_start, size_t cut_limit,
                   uint32_t window_w, uint32_t mask, const uint32_t gear[256],
                   size_t* out_cut, bool* found, uint64_t* probes) {
  if (scan_start < window_w || scan_start >= cut_limit) {
    if (found) *found = false;
    return false;
  }
  return ScanGearCut(data, scan_start, cut_limit, window_w, mask, gear, out_cut,
                     found, probes);
}

bool GtCdcScanGearNc(const uint8_t* data, size_t scan_start, size_t cut_limit,
                     uint32_t window_w, uint32_t mask, uint32_t norm_mask,
                     const uint32_t gear[256], size_t* out_cut, bool* found,
                     uint64_t* probes) {
  if (scan_start < window_w || scan_start >= cut_limit) {
    if (found) *found = false;
    return false;
  }
  return ScanGearCutNc(data, scan_start, cut_limit, window_w, mask, norm_mask,
                       gear, out_cut, found, probes);
}

namespace {

bool ScanGearViewManual(const GtCdcSegmentView& view, size_t scan_start,
                        size_t cut_limit, uint32_t window_w, uint32_t mask,
                        uint32_t norm_mask, const uint32_t gear[256],
                        size_t* out_cut, bool* found, uint64_t* probes) {
  uint32_t h = 0;
  for (size_t i = scan_start - window_w; i < scan_start; ++i) {
    h = (h << 1) + gear[view.at(i)];
  }
  for (size_t i = scan_start; i < cut_limit; ++i) {
    if (probes) ++*probes;
    const bool base_hit = (h & mask) == 0;
    const bool nc_hit = norm_mask == 0 || (h & norm_mask) == norm_mask;
    if (base_hit && nc_hit) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h = (h << 1) + gear[view.at(i)] - gear[view.at(i - window_w)];
  }
  return false;
}

}  // namespace

bool GtCdcScanGearView(const GtCdcSegmentView& view, size_t scan_start,
                       size_t cut_limit, uint32_t window_w, uint32_t mask,
                       const uint32_t gear[256], size_t* out_cut, bool* found,
                       uint64_t* probes) {
  if (!out_cut || !found || scan_start >= cut_limit) return false;
  *found = false;
  if (scan_start < window_w) return false;

  if (view.len0 == 0) {
    return ScanGearCut(view.seg1, scan_start, cut_limit, window_w, mask, gear,
                       out_cut, found, probes);
  }

  if (scan_start >= view.len0 && cut_limit <= view.len0 + view.len1) {
    size_t rel_cut = cut_limit - view.len0;
    const bool ok = ScanGearCut(view.seg1, scan_start - view.len0, rel_cut,
                                window_w, mask, gear, &rel_cut, found, probes);
    if (*found) *out_cut = rel_cut + view.len0;
    return ok;
  }

  return ScanGearViewManual(view, scan_start, cut_limit, window_w, mask, 0,
                            gear, out_cut, found, probes);
}

bool GtCdcScanGearNcView(const GtCdcSegmentView& view, size_t scan_start,
                         size_t cut_limit, uint32_t window_w, uint32_t mask,
                         uint32_t norm_mask, const uint32_t gear[256],
                         size_t* out_cut, bool* found, uint64_t* probes) {
  if (!out_cut || !found || scan_start >= cut_limit) return false;
  *found = false;
  if (scan_start < window_w) return false;

  if (view.len0 == 0) {
    return ScanGearCutNc(view.seg1, scan_start, cut_limit, window_w, mask,
                         norm_mask, gear, out_cut, found, probes);
  }

  if (scan_start >= view.len0 && cut_limit <= view.len0 + view.len1) {
    size_t rel_cut = cut_limit - view.len0;
    const bool ok = ScanGearCutNc(view.seg1, scan_start - view.len0, rel_cut,
                                  window_w, mask, norm_mask, gear, &rel_cut,
                                  found, probes);
    if (*found) *out_cut = rel_cut + view.len0;
    return ok;
  }

  return ScanGearViewManual(view, scan_start, cut_limit, window_w, mask,
                            norm_mask, gear, out_cut, found, probes);
}

bool GtCdcScanGearAn(const uint8_t* data, size_t scan_start, size_t cut_limit,
                     size_t chunk_start, size_t pos_bias, uint32_t window_w,
                     uint32_t avg_size, uint8_t nc_level, uint32_t seed,
                     const uint32_t gear[256], size_t* out_cut, bool* found) {
  if (scan_start < window_w || scan_start >= cut_limit) {
    if (found) *found = false;
    return false;
  }
  return ScanGearCutAn(data, scan_start, cut_limit, chunk_start, pos_bias,
                       window_w, avg_size, nc_level, seed, gear, out_cut,
                       found);
}

bool ScanGearViewManualAn(const GtCdcSegmentView& view, size_t scan_start,
                          size_t cut_limit, size_t chunk_start, size_t pos_bias,
                          uint32_t window_w, uint32_t avg_size,
                          uint8_t nc_level, uint32_t seed,
                          const uint32_t gear[256], size_t* out_cut,
                          bool* found) {
  uint32_t h = 0;
  for (size_t i = scan_start - window_w; i < scan_start; ++i) {
    h = (h << 1) + gear[view.at(i)];
  }
  for (size_t i = scan_start; i < cut_limit; ++i) {
    const size_t abs_pos = pos_bias + i;
    const size_t d = abs_pos - chunk_start;
    const uint32_t mask = SelectPhaseMask(d, avg_size, nc_level);
    const uint32_t h_test = MixSeedHash(h, seed, abs_pos);
    if ((h_test & mask) == 0) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h = (h << 1) + gear[view.at(i)] - gear[view.at(i - window_w)];
  }
  return false;
}

bool GtCdcScanGearAnView(const GtCdcSegmentView& view, size_t scan_start,
                         size_t cut_limit, size_t chunk_start, size_t pos_bias,
                         uint32_t window_w, uint32_t avg_size,
                         uint8_t nc_level, uint32_t seed,
                         const uint32_t gear[256], size_t* out_cut,
                         bool* found) {
  if (!out_cut || !found || scan_start >= cut_limit) return false;
  *found = false;
  if (scan_start < window_w) return false;

  if (view.len0 == 0) {
    return ScanGearCutAn(view.seg1, scan_start, cut_limit, chunk_start,
                         pos_bias, window_w, avg_size, nc_level, seed, gear,
                         out_cut, found);
  }

  if (scan_start >= view.len0 && cut_limit <= view.len0 + view.len1) {
    size_t rel_cut = cut_limit - view.len0;
    const bool ok = ScanGearCutAn(
        view.seg1, scan_start - view.len0, rel_cut, chunk_start,
        pos_bias + view.len0, window_w, avg_size, nc_level, seed, gear,
        &rel_cut, found);
    if (*found) *out_cut = rel_cut + view.len0;
    return ok;
  }

  return ScanGearViewManualAn(view, scan_start, cut_limit, chunk_start,
                              pos_bias, window_w, avg_size, nc_level, seed,
                              gear, out_cut, found);
}

bool GtCdcScanGear2F(const uint8_t* data, size_t scan_start, size_t cut_limit,
                     uint32_t window_w, uint32_t mask_lo, uint32_t mask_hi,
                     uint32_t norm_shift, const uint32_t gear[256],
                     const uint32_t norm_gear[256], size_t* out_cut,
                     bool* found, uint64_t* probes) {
  if (scan_start < window_w || scan_start >= cut_limit) {
    if (found) *found = false;
    return false;
  }
  return ScanGearCut2F(data, scan_start, cut_limit, window_w, mask_lo, mask_hi,
                       norm_shift, gear, norm_gear, out_cut, found, 0, 0, false,
                       nullptr, nullptr, probes);
}

namespace {

bool ScanGearViewManual2F(const GtCdcSegmentView& view, size_t scan_start,
                          size_t cut_limit, uint32_t window_w, uint32_t mask_lo,
                          uint32_t mask_hi, uint32_t norm_shift,
                          const uint32_t gear[256], const uint32_t norm_gear[256],
                          size_t* out_cut, bool* found, uint64_t* probes) {
  uint32_t h_gear = 0;
  uint32_t h_norm = 0;
  for (size_t i = scan_start - window_w; i < scan_start; ++i) {
    h_gear = (h_gear << 1) + gear[view.at(i)];
    h_norm = (h_norm << 1) + norm_gear[view.at(i)];
  }
  for (size_t i = scan_start; i < cut_limit; ++i) {
    if (probes) ++*probes;
    if ((h_gear & mask_lo) == 0 && ((h_norm >> norm_shift) & mask_hi) == 0) {
      *out_cut = i;
      *found = true;
      return true;
    }
    h_gear = (h_gear << 1) + gear[view.at(i)] - gear[view.at(i - window_w)];
    h_norm = (h_norm << 1) + norm_gear[view.at(i)] -
             norm_gear[view.at(i - window_w)];
  }
  return false;
}

}  // namespace

bool GtCdcScanGear2FView(const GtCdcSegmentView& view, size_t scan_start,
                         size_t cut_limit, uint32_t window_w, uint32_t mask_lo,
                         uint32_t mask_hi, uint32_t norm_shift,
                         const uint32_t gear[256], const uint32_t norm_gear[256],
                         size_t* out_cut, bool* found, uint32_t h_gear_resume,
                         uint32_t h_norm_resume, bool use_h_resume,
                         uint32_t* h_gear_out, uint32_t* h_norm_out,
                         uint64_t* probes) {
  if (!out_cut || !found || scan_start >= cut_limit) return false;
  *found = false;
  if (scan_start < window_w) return false;

  if (view.len0 == 0) {
    return ScanGearCut2F(view.seg1, scan_start, cut_limit, window_w, mask_lo,
                         mask_hi, norm_shift, gear, norm_gear, out_cut, found,
                         h_gear_resume, h_norm_resume, use_h_resume, h_gear_out,
                         h_norm_out, probes);
  }

  if (scan_start >= view.len0 && cut_limit <= view.len0 + view.len1) {
    size_t rel_cut = cut_limit - view.len0;
    const bool ok = ScanGearCut2F(
        view.seg1, scan_start - view.len0, rel_cut, window_w, mask_lo, mask_hi,
        norm_shift, gear, norm_gear, &rel_cut, found, h_gear_resume,
        h_norm_resume, use_h_resume, h_gear_out, h_norm_out, probes);
    if (*found) *out_cut = rel_cut + view.len0;
    return ok;
  }

  return ScanGearViewManual2F(view, scan_start, cut_limit, window_w, mask_lo,
                              mask_hi, norm_shift, gear, norm_gear, out_cut,
                              found, probes);
}

}  // namespace gtcdc_internal
}  // namespace ebbackup
