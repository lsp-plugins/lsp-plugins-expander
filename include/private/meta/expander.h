/*
 * Copyright (C) 2023 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2023 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-expander
 * Created on: 3 авг. 2021 г.
 *
 * lsp-plugins-expander is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-expander is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-expander. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PRIVATE_META_EXPANDER_H_
#define PRIVATE_META_EXPANDER_H_

#include <lsp-plug.in/plug-fw/meta/types.h>
#include <lsp-plug.in/plug-fw/const.h>


namespace lsp
{
    namespace meta
    {
        struct expander_metadata
        {
            static constexpr float  ATTACK_LVL_MIN          = GAIN_AMP_M_60_DB;
            static constexpr float  ATTACK_LVL_MAX          = GAIN_AMP_0_DB;
            static constexpr float  ATTACK_LVL_DFL          = GAIN_AMP_M_12_DB;
            static constexpr float  ATTACK_LVL_STEP         = 0.05f;

            static constexpr float  RELEASE_LVL_MIN         = GAIN_AMP_M_INF_DB;
            static constexpr float  RELEASE_LVL_MAX         = GAIN_AMP_P_36_DB;
            static constexpr float  RELEASE_LVL_DFL         = GAIN_AMP_M_INF_DB;
            static constexpr float  RELEASE_LVL_STEP        = 0.05f;

            static constexpr float  ATTACK_TIME_MIN         = 0.0f;
            static constexpr float  ATTACK_TIME_MAX         = 2000.0f;
            static constexpr float  ATTACK_TIME_DFL         = 20.0f;
            static constexpr float  ATTACK_TIME_STEP        = 0.0025f;

            static constexpr float  RELEASE_TIME_MIN        = 0.0f;
            static constexpr float  RELEASE_TIME_MAX        = 5000.0f;
            static constexpr float  RELEASE_TIME_DFL        = 100.0f;
            static constexpr float  RELEASE_TIME_STEP       = 0.0025f;

            static constexpr float  HOLD_TIME_MIN           = 0.0f;
            static constexpr float  HOLD_TIME_MAX           = 1000.0f;
            static constexpr float  HOLD_TIME_DFL           = 0.0f;
            static constexpr float  HOLD_TIME_STEP          = 0.1f;

            static constexpr float  KNEE_MIN                = GAIN_AMP_M_24_DB;
            static constexpr float  KNEE_MAX                = GAIN_AMP_0_DB;
            static constexpr float  KNEE_DFL                = GAIN_AMP_M_6_DB;
            static constexpr float  KNEE_STEP               = 0.01f;

            static constexpr float  MAKEUP_MIN              = GAIN_AMP_M_60_DB;
            static constexpr float  MAKEUP_MAX              = GAIN_AMP_P_60_DB;
            static constexpr float  MAKEUP_DFL              = GAIN_AMP_0_DB;
            static constexpr float  MAKEUP_STEP             = 0.05f;

            static constexpr float  RATIO_MIN               = 1.0f;
            static constexpr float  RATIO_MAX               = 100.0f;
            static constexpr float  RATIO_DFL               = 4.0f;
            static constexpr float  RATIO_STEP              = 0.0025f;

            static constexpr float  LOOKAHEAD_MIN           = 0.0f;
            static constexpr float  LOOKAHEAD_MAX           = 20.0f;
            static constexpr float  LOOKAHEAD_DFL           = 0.0f;
            static constexpr float  LOOKAHEAD_STEP          = 0.01f;

            static constexpr float  REACTIVITY_MIN          = 0.000;    // Minimum reactivity [ms]
            static constexpr float  REACTIVITY_MAX          = 250;      // Maximum reactivity [ms]
            static constexpr float  REACTIVITY_DFL          = 10;       // Default reactivity [ms]
            static constexpr float  REACTIVITY_STEP         = 0.01;     // Reactivity step

            static constexpr size_t SC_MODE_DFL             = 1;
            static constexpr size_t SC_SOURCE_DFL           = 0;
            static constexpr size_t SC_SPLIT_SOURCE_DFL     = 0;
            static constexpr size_t SC_TYPE_DFL             = 0;

            static constexpr float  HPF_MIN                 = 10.0f;
            static constexpr float  HPF_MAX                 = 20000.0f;
            static constexpr float  HPF_DFL                 = 10.0f;
            static constexpr float  HPF_STEP                = 0.0025f;

            static constexpr float  LPF_MIN                 = 10.0f;
            static constexpr float  LPF_MAX                 = 20000.0f;
            static constexpr float  LPF_DFL                 = 20000.0f;
            static constexpr float  LPF_STEP                = 0.0025f;

            static constexpr size_t CURVE_MESH_SIZE         = 256;
            static constexpr float  CURVE_DB_MIN            = -72;
            static constexpr float  CURVE_DB_MAX            = +24;

            static constexpr size_t TIME_MESH_SIZE          = 400;
            static constexpr float  TIME_HISTORY_MAX        = 5.0f;

            enum mode_t
            {
                EM_DOWNWARD,
                EM_UPWARD
            };

            static constexpr size_t EM_DEFAULT              = EM_UPWARD;
        };

        extern const meta::plugin_t expander_mono;
        extern const meta::plugin_t expander_stereo;
        extern const meta::plugin_t expander_lr;
        extern const meta::plugin_t expander_ms;
        extern const meta::plugin_t sc_expander_mono;
        extern const meta::plugin_t sc_expander_stereo;
        extern const meta::plugin_t sc_expander_lr;
        extern const meta::plugin_t sc_expander_ms;
    } /* namespace meta */
} /* namespace lsp */


#endif /* PRIVATE_META_EXPANDER_H_ */
