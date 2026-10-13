/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * www.aomedia.org/license/patent.
 */

#include "custom_gain_calculator.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "definitions.h"
#include "cvalue.h"
#include "cvector.h"
#include "dbap_panner_2d.h"
#include "gain_calculator.h"
#include "layout.h"
#include "numc.h"
#include "oar_config.h"
#include "vbap_panner_2d.h"
#include "vog.h"

typedef struct LayerwisePanner {
  vector_t *ref_layers;
} layerwise_panner_t;

struct CustomGainCalculator {
  layout_gain_calculator_t layout;
  const speaker_layout_t *speaker_layout_wo_lfe;
  vector_t *layers;  // auto_float_t
  layerwise_panner_t layerwise_panner;
  vector_t *vbap_panners;  // calculator_t *
  vector_t *dbap_panners;  // calculator_t *
  vector_t *speaker_idxs;  // elevation_layer_t
};

def_key_value_type(auto_float_t, vector_t *,
                   elevation_layer);  // speaker_index_t or region_t
def_key_value_type(auto_float_t, gain_calculator_t *, calculator);
def_key_value_type(auto_float_t, auto_float_t, elevation_gain);

/* The most layer gains one gain calculation pans between. */
#define def_max_layer_gains 6


// https://www.dsprelated.com/showarticle/1230.php
static int tanlaw(auto_float_t source_angle, auto_float_t speaker_angle,
                  auto_float_t gains[2]) {
  auto_float_t c, s, gains_norm;

  if (source_angle > speaker_angle) return -22;
  if (speaker_angle < 0.f || speaker_angle >= 90.f) return -22;

  c = nc_cos(nc_radians(source_angle)) / nc_cos(nc_radians(speaker_angle));
  s = nc_sin(nc_radians(source_angle)) / nc_sin(nc_radians(speaker_angle));
  gains[0] = c + s;
  gains[1] = c - s;
  gains_norm = nc_linalg_norm(gains, 2);

  if (gains_norm == 0.f) return -22;

  gains[0] /= gains_norm;
  gains[1] /= gains_norm;

  return 0;
}

static int __find_elevation(value_wrap_t a, value_wrap_t b) {
  return def_value_wrap_float(&a) == def_value_wrap_float(&b);
}

/* Writes the elevation layers a source at `elevation` pans between, and the
 * gain of each, into gains[0..1]. Returns the number written (1 or 2), or
 * -22 when the layout has more layers than def_max_speakers. */
static int layerwise_panner_calculate_gains(layerwise_panner_t *self,
                                            auto_float_t elevation,
                                            elevation_gain_t gains[2]) {
  auto_float_t min_ele = def_none_degree;
  auto_float_t max_ele = -def_none_degree;
  auto_float_t el = 0.f;
  int n = vector_size(self->ref_layers);
  value_wrap_t v;

  if (n > def_max_speakers) return -22;

  for (int i = 0; i < n; ++i) {
    el = def_value_wrap_float(vector_at(self->ref_layers, i));
    if (el < min_ele) min_ele = el;
    if (el > max_ele) max_ele = el;
  }

  if (vector_find(self->ref_layers, def_value_wrap_instance_float(elevation),
                  __find_elevation, &v) >= 0) {
    gains[0] = (elevation_gain_t){elevation, 1.0f};
    return 1;
  } else if (elevation < min_ele) {
    gains[0] = (elevation_gain_t){min_ele, 1.0};
    return 1;
  } else if (elevation > max_ele) {
    gains[0] = (elevation_gain_t){max_ele, 1.0};
    return 1;
  } else {
    auto_float_t ele[2];
    auto_float_t speaker_angle;
    auto_float_t ele_gains[2];
    int sortidxs[def_max_speakers];
    auto_float_t abs_diffs[def_max_speakers];

    for (int i = 0; i < n; ++i) {
      el = def_value_wrap_float(vector_at(self->ref_layers, i));
      abs_diffs[i] = nc_abs(el - elevation);
    }

    nc_argsort(abs_diffs, n, sortidxs);
    ele[0] = def_value_wrap_float(vector_at(self->ref_layers, sortidxs[0]));
    for (int i = 1; i < n; ++i) {
      el = def_value_wrap_float(vector_at(self->ref_layers, sortidxs[i]));
      if (nc_sign(ele[0] - elevation) != nc_sign(el - elevation)) {
        ele[1] = el;
        break;
      }
    }

    speaker_angle = nc_abs(ele[1] - ele[0]) / 2;
    tanlaw(speaker_angle - nc_abs(ele[0] - elevation), speaker_angle,
           ele_gains);

    for (int i = 0; i < 2; ++i) {
      gains[i] = (elevation_gain_t){ele[i], ele_gains[i]};
#ifdef __dbg__
      trace("ele: %" def_f8g ", gain: %" def_f8g "", ele[i], ele_gains[i]);
#endif
    }
    return 2;
  }
}

static void _custom_gain_calculator_destroy(custom_gain_calculator_t *self);
static int _custom_gain_calculator_calculate_gains(
    custom_gain_calculator_t *self, auto_float_t azimuth,
    auto_float_t elevation, auto_float_t distance, auto_float_t *gains, int n);

static int __create_speaker_indexs(custom_gain_calculator_t *self);
static int __find_calculator_with_elevation(value_wrap_t a, value_wrap_t b);
static int __find_elevation_layer(value_wrap_t a, value_wrap_t b);
static void __calculator_free(calculator_t *calculator);
static void __speaker_indices_free(elevation_layer_t *layer);
static int __get_layer_max_min_elevation(custom_gain_calculator_t *self,
                                         auto_float_t *max_elevation,
                                         auto_float_t *min_elevation);

static vector_t *_get_elevation_layers(custom_gain_calculator_t *self) {
  vector_t *layers = vector_new();
  value_wrap_t v;
  if (!layers) return 0;
  for (int i = 0; i < self->speaker_layout_wo_lfe->num_speakers; ++i) {
    auto_float_t elevation =
        self->speaker_layout_wo_lfe->speakers[i]->elevation;
    if (vector_find(layers, def_value_wrap_instance_float(elevation),
                    __find_elevation, &v) < 0)
      vector_push(layers, def_value_wrap_instance_float(elevation));
  }
  return layers;
}

static int _create_panners_for_2d(custom_gain_calculator_t *self) {
  speaker_position_t **speakers =
      (speaker_position_t **)self->speaker_layout_wo_lfe->speakers;
  int s = get_speaker_count(self->speaker_layout_wo_lfe);
  int n = vector_size(self->layers);
  calculator_t *elevation_calculator = 0;

  self->vbap_panners = vector_new();
  self->dbap_panners = vector_new();

  for (int i = 0; i < n; ++i) {
    int m;
    elevation_layer_t *elevation_layer =
        def_value_wrap_ptr(vector_at(self->speaker_idxs, i));
    auto_float_t el = def_value_wrap_float(vector_at(self->layers, i));
    vector_t *layout_2d = vector_new();

    if (!layout_2d) continue;

    for (int j = 0; j < s; ++j) {
      if (speakers[j]->elevation == el) {
        vector_push(layout_2d, def_value_wrap_instance_ptr(speakers[j]));
        vector_push(elevation_layer->value, def_value_wrap_instance_i32(j));
      }
    }

    m = vector_size(layout_2d);

    elevation_calculator = def_mallocz(calculator_t, 1);
    if (!elevation_calculator) {
      vector_free(layout_2d, 0);
      continue;
    }
    elevation_calculator->key = el;
    if (m != 1)
      elevation_calculator->value = vbap_panner_2d_create(layout_2d);
    else
      elevation_calculator->value = vog_create();
    vector_push(self->vbap_panners,
                def_value_wrap_instance_ptr(elevation_calculator));

    elevation_calculator = def_mallocz(calculator_t, 1);
    if (!elevation_calculator) {
      vector_free(layout_2d, 0);
      continue;
    }
    elevation_calculator->key = el;
    if (m != 1)
      elevation_calculator->value = dbap_panner_2d_create(layout_2d);
    else
      elevation_calculator->value = vog_create();
    vector_push(self->dbap_panners,
                def_value_wrap_instance_ptr(elevation_calculator));

    vector_free(layout_2d, 0);
  }

  return 0;
}

custom_gain_calculator_t *custom_gain_calculator_create(
    const speaker_layout_t *speaker_layout) {
  custom_gain_calculator_t *self = def_mallocz(custom_gain_calculator_t, 1);
  if (!self) return 0;

#ifdef __dbg__
  trace("speaker layout: %p(%d)", speaker_layout, speaker_layout->layout);
#endif

  self->layout.layout = speaker_layout;
  self->layout.base.calculate_gains = def_gain_calculator_calculate_gains(
      _custom_gain_calculator_calculate_gains);
  self->layout.base.destroy =
      def_gain_calculator_destroy(_custom_gain_calculator_destroy);

  self->speaker_layout_wo_lfe = get_layout_without_lfe(speaker_layout->layout);
  self->layers = _get_elevation_layers(self);
  if (!self->layers) {
    _custom_gain_calculator_destroy(self);
    return 0;
  } else {
#ifdef __dbg__
    int n = vector_size(self->layers);
    for (int i = 0; i < n; ++i) {
      debug("layer(%" def_f8g ")",
            def_value_wrap_float(vector_at(self->layers, i)));
    }
#endif
  }

  __create_speaker_indexs(self);
  self->layerwise_panner.ref_layers = self->layers;
  _create_panners_for_2d(self);

  return self;
}

void _custom_gain_calculator_destroy(custom_gain_calculator_t *self) {
  if (!self) return;
  if (self->layers) vector_free(self->layers, 0);
  if (self->vbap_panners)
    vector_free(self->vbap_panners, def_default_free_ptr(__calculator_free));
  if (self->dbap_panners)
    vector_free(self->dbap_panners, def_default_free_ptr(__calculator_free));
  if (self->speaker_idxs)
    vector_free(self->speaker_idxs,
                def_default_free_ptr(__speaker_indices_free));
  free(self);
}

int _custom_gain_calculator_calculate_gains(custom_gain_calculator_t *self,
                                            auto_float_t azimuth,
                                            auto_float_t elevation,
                                            auto_float_t distance,
                                            auto_float_t *gains, int n) {
  int n_wo_lfe = self->speaker_layout_wo_lfe->num_speakers;
  auto_float_t gains_wo_lfe[def_max_speakers] = {0};
  /* The layers the source pans between: up to two, and up to two more for
   * each of those that splits off the back of an asymmetric layout. */
  elevation_gain_t layer_gains[def_max_layer_gains];

  auto_float_t gains_wo_lfe_norm = 0.f;
  auto_float_t new_elevation = def_none_degree;

  if (n_wo_lfe > def_max_speakers) return -22;

  int l = layerwise_panner_calculate_gains(&self->layerwise_panner, elevation,
                                           layer_gains);
  int layer_cnt = l;

  if (l < 0) return l;

  for (int i = 0; i < n; ++i) gains[i] = 0.0f;

  for (int i = 0; i < l; ++i) {
    elevation_gain_t *ele_gain = &layer_gains[i];
    auto_float_t dbap_dist = 0.f, alpha = 0.f, gain_2d_norm = 0.f,
                 max = -def_none_degree, min = def_none_degree;
    auto_float_t vbap_gains[def_max_speakers], dbap_gains[def_max_speakers],
        gains_2d[def_max_speakers];
    calculator_t *calculator = 0;
    elevation_layer_t *layer = 0;
    value_wrap_t v;

#ifdef __dbg__
    debug("%d, layer el %f, gain %f", i, ele_gain->key, ele_gain->value);
#endif

    --layer_cnt;

    if (vector_find(self->vbap_panners,
                    def_value_wrap_instance_float(ele_gain->key),
                    __find_calculator_with_elevation, &v) >= 0)

      calculator = v.ptr;

    // Support back speaker of asymmetric layout except middle layout by
    // separating to other layer
    if (layer_cnt >= 0 && ele_gain->key != 0.0f &&
        (calculator && !vbap_panner_2d_is_convex_hull(calculator->value))) {
      auto_float_t max_az = -def_none_degree;
      vector_t *target_speakers =
          vbap_panner_2d_get_speaker_positions(calculator->value);
      int spn = vector_size(target_speakers);

      for (int j = 0; j < spn; ++j) {
        speaker_position_t *sp =
            def_value_wrap_ptr(vector_at(target_speakers, j));
        if (sp->azimuth > max_az) max_az = sp->azimuth;
      }

      // If source in asymmetric layout and out of azimuth, separate to another
      // layer by modify elevation
      if (nc_abs(azimuth) > max_az) {
        elevation_gain_t layerwise_gains[2];
        int nn = 0;
        new_elevation =
            ele_gain->key * (1 - ((nc_abs(azimuth) - max_az) / (180 - max_az)));
#ifdef __dbg__
        trace("new elevation %g", new_elevation);
#endif
        nn = layerwise_panner_calculate_gains(&self->layerwise_panner,
                                              new_elevation, layerwise_gains);
        for (int j = 0; j < nn && l < def_max_layer_gains; ++j) {
          layerwise_gains[j].value *= ele_gain->value;
          layer_gains[l++] = layerwise_gains[j];
        }
        continue;
      }
    }

    memset(vbap_gains, 0, sizeof(auto_float_t) * n_wo_lfe);
    memset(dbap_gains, 0, sizeof(auto_float_t) * n_wo_lfe);
    memset(gains_2d, 0, sizeof(auto_float_t) * n_wo_lfe);

    // Apply new elevation for seperated source from asymmetric layout
    if (layer_cnt < 0) elevation = new_elevation;

    // Distance-reducing projection to closest layer if outside the hull
    __get_layer_max_min_elevation(self, &max, &min);
    if (elevation > max || elevation < min)
      distance *= nc_cos(nc_radians(elevation));
#ifdef __dbg__
    trace("max: %g, min %g, elevation %g", max, min, elevation);
#endif

    // Calculate VBAP + DBAP

    if (calculator) {
      auto_float_t final_azimuth = azimuth;

#ifdef __dbg__
      trace("calculate vbap gains with az,el,dist (%" def_f8g ",%" def_f8g
            ",%" def_f8g ") in layer %" def_f8g ".",
            azimuth, elevation, 1.0f, ele_gain->key);
#endif

      // If asymmetric layout, front-back symmetry the back side azimuth of
      // layout
      if (!vbap_panner_2d_is_convex_hull(calculator->value)) {
        if (final_azimuth > 90 && final_azimuth <= 180) {
          final_azimuth = 180 - final_azimuth;
        } else if (final_azimuth >= -180 && final_azimuth < -90) {
          final_azimuth = -180 - final_azimuth;
        }
      }
      calculator->value->calculate_gains(calculator->value, final_azimuth,
                                         elevation, 1.0, vbap_gains, n_wo_lfe);
    }

    // Utilize 3 distinct zones with different panning methods
    if (distance < 0.5) {
      /* weight DBAP source at distance = 0 more in panning than VBAP source at
       * distance 1 */
      dbap_dist = 0;
      alpha = distance;
    } else if (distance < 0.9) {
      /* make a DBAP source for complementary distance as VBAP source and pan
       * between these two equally */
      dbap_dist = 2 * distance - 1;
      alpha = 0.5;
    } else {
      /* we want to end up with pure VBAP for distance 1 and have a smooth
       * transition */
      dbap_dist = 0.8;
      alpha = 0.5 + 0.5 * (distance - 0.9) / 0.1;

      if (alpha > 1.0) alpha = 1.0;
    }

#ifdef __dbg__
    trace("distance %g, dbap distance %g, alpha %g", distance, dbap_dist,
          alpha);
#endif
    calculator = 0;
    if (vector_find(self->dbap_panners,
                    def_value_wrap_instance_float(ele_gain->key),
                    __find_calculator_with_elevation, &v) >= 0)
      calculator = v.ptr;
    if (calculator) {
#ifdef __dbg__
      trace("calculate dbap gains with az,el,dist: %" def_f8g ",%" def_f8g
            ",%" def_f8g ") in layer %" def_f8g ".",
            azimuth, elevation, dbap_dist, ele_gain->key);
#endif
      calculator->value->calculate_gains(calculator->value, azimuth, elevation,
                                         dbap_dist, dbap_gains, n_wo_lfe);
    }

    for (int j = 0; j < n_wo_lfe; ++j)
      gains_2d[j] = alpha * vbap_gains[j] + (1 - alpha) * dbap_gains[j];
    gain_2d_norm = nc_linalg_norm(gains_2d, n_wo_lfe);
    for (int j = 0; j < n_wo_lfe; ++j) gains_2d[j] /= (gain_2d_norm + 1e-10);

    if (vector_find(self->speaker_idxs,
                    def_value_wrap_instance_float(ele_gain->key),
                    __find_elevation_layer, &v) >= 0)
      layer = v.ptr;
    if (layer) {
      vector_t *si = layer->value;
      int s = vector_size(si);

#ifdef __dbg__
      debug_array_float("vbap gains", vbap_gains, s);
      debug_array_float("dbap gains", dbap_gains, s);
      debug_array_float("2d gains", gains_2d, s);
#endif

      for (int j = 0; j < s; ++j) {
        int32_t idx = def_value_wrap_i32(vector_at(si, j));
        gains_wo_lfe[idx] += gains_2d[j] * ele_gain->value;
      }
    }
  }

#ifdef __dbg__
  debug_array_float("custom gains", gains_wo_lfe, n_wo_lfe);
#endif

  gains_wo_lfe_norm = nc_linalg_norm(gains_wo_lfe, n_wo_lfe);
#ifdef __dbg__
  trace("gains norm: %" def_f8g "", gains_wo_lfe_norm);
#endif

  for (int i = 0, j = 0; i < self->layout.layout->num_speakers; ++i) {
    if (gains_wo_lfe_norm != 0.0 && !is_lfe(self->layout.layout->speakers[i])) {
      gains[i] = gains_wo_lfe[j++] / gains_wo_lfe_norm;
    }
  }

  return 0;
}

// ============================================================================
// private functions
// ============================================================================

int __create_speaker_indexs(custom_gain_calculator_t *self) {
  int n = vector_size(self->layers);

  self->speaker_idxs = vector_new();
  for (int i = 0; i < n; ++i) {
    vector_t *indices = vector_new();
    elevation_layer_t *layer = def_mallocz(elevation_layer_t, 1);
    if (layer) {
      layer->key = def_value_wrap_float(vector_at(self->layers, i));
      layer->value = indices;
    }
    vector_push(self->speaker_idxs, def_value_wrap_instance_ptr(layer));
  }

  return 0;
}

int __find_calculator_with_elevation(value_wrap_t a, value_wrap_t b) {
  calculator_t *calculator = a.ptr;
  return calculator->key == def_value_wrap_float(&b);
}

int __find_elevation_layer(value_wrap_t a, value_wrap_t b) {
  elevation_layer_t *layer = a.ptr;
  return layer->key == def_value_wrap_float(&b);
}

void __calculator_free(calculator_t *calculator) {
  if (!calculator) return;
  if (calculator->value) calculator->value->destroy(calculator->value);
  free(calculator);
}

void __speaker_indices_free(elevation_layer_t *layer) {
  if (!layer) return;
  if (layer->value) vector_free(layer->value, 0);
  free(layer);
}

int __get_layer_max_min_elevation(custom_gain_calculator_t *self,
                                  auto_float_t *max_elevation,
                                  auto_float_t *min_elevation) {
  auto_float_t min = def_none_degree, max = -def_none_degree;
  auto_float_t el;
  int n = vector_size(self->layers);
  for (int i = 0; i < n; ++i) {
    el = def_value_wrap_float(vector_at(self->layers, i));
    if (el > max) max = el;
    if (el < min) min = el;
  }
  *max_elevation = max;
  *min_elevation = min;

  return 0;
}
