/*
 * Copyright (C) 2025 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2025 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/shared/meta/developers.h>
#include <private/meta/expander.h>

#define LSP_PLUGINS_EXPANDER_VERSION_MAJOR       1
#define LSP_PLUGINS_EXPANDER_VERSION_MINOR       0
#define LSP_PLUGINS_EXPANDER_VERSION_MICRO       29

#define LSP_PLUGINS_EXPANDER_VERSION  \
    LSP_MODULE_VERSION( \
        LSP_PLUGINS_EXPANDER_VERSION_MAJOR, \
        LSP_PLUGINS_EXPANDER_VERSION_MINOR, \
        LSP_PLUGINS_EXPANDER_VERSION_MICRO  \
    )

namespace lsp
{
    namespace meta
    {
        //-------------------------------------------------------------------------
        // Expander
        static const int plugin_classes[]       = { C_EXPANDER, -1 };
        static const int clap_features_mono[]   = { CF_AUDIO_EFFECT, CF_MONO, -1 };
        static const int clap_features_stereo[] = { CF_AUDIO_EFFECT, CF_STEREO, -1 };

        static const port_item_t exp_sc_modes[] =
        {
            { "Peak",           "sidechain.peak"            },
            { "RMS",            "sidechain.rms"             },
            { "LPF",            "sidechain.lpf"             },
            { "SMA",            "sidechain.sma"             },
            { NULL, NULL }
        };

        static const port_item_t exp_sc_sources[] =
        {
            { "Middle",         "sidechain.middle"          },
            { "Side",           "sidechain.side"            },
            { "Left",           "sidechain.left"            },
            { "Right",          "sidechain.right"           },
            { "Min",            "sidechain.min"             },
            { "Max",            "sidechain.max"             },
            { NULL, NULL }
        };

        static const port_item_t exp_sc_split_sources[] =
        {
            { "Left/Right",     "sidechain.left_right"      },
            { "Right/Left",     "sidechain.right_left"      },
            { "Mid/Side",       "sidechain.mid_side"        },
            { "Side/Mid",       "sidechain.side_mid"        },
            { "Min",            "sidechain.min"             },
            { "Max",            "sidechain.max"             },
            { NULL, NULL }
        };

        static const port_item_t exp_sc_type[] =
        {
            { "Internal",   "sidechain.internal"        },
            { "Link",       "sidechain.link"            },
            { NULL, NULL }
        };

        static const port_item_t exp_extsc_type[] =
        {
            { "External",   "sidechain.external"        },
            { "Internal",   "sidechain.internal"        },
            { "Link",       "sidechain.link"            },
            { NULL, NULL }
        };

        static const port_item_t exp_modes[] =
        {
            { "Down",       "expander.down_ward"        },
            { "Up",         "expander.up_ward"          },
            { NULL, NULL }
        };

        static const port_item_t exp_filter_slope[] =
        {
            { "off",        "eq.slope.off"              },
            { "12 dB/oct",  "eq.slope.12dbo"            },
            { "24 dB/oct",  "eq.slope.24dbo"            },
            { "36 dB/oct",  "eq.slope.36dbo"            },
            { NULL, NULL }
        };

        #define EXP_COMMON     \
            BYPASS,             \
            IN_GAIN,            \
            OUT_GAIN,           \
            SWITCH("showmx", "Show mix overlay", "Show mix bar", 0.0f), \
            SWITCH("showsc", "Show sidechain overlay", "Show SC bar", 0.0f), \
            SWITCH("pause", "Pause graph analysis", "Pause", 0.0f), \
            TRIGGER("clear", "Clear graph analysis", "Clear")

        #define EXP_MS_COMMON  \
            EXP_COMMON,        \
            SWITCH("msl", "Mid/Side listen", "M/S listen", 0.0f)

        #define EXP_SPLIT_COMMON \
            SWITCH("ssplit", "Stereo split", "Stereo split", 0.0f), \
            COMBO("sscs", "Split sidechain source", "Split SC source", expander_metadata::SC_SPLIT_SOURCE_DFL, exp_sc_split_sources)

        #define EXP_SHM_LINK_MONO \
            OPT_RETURN_MONO("link", "shml", "Side-chain shared memory link")

        #define EXP_SHM_LINK_STEREO \
            OPT_RETURN_STEREO("link", "shml_", "Side-chain shared memory link")

        #define EXP_MONO_CHANNEL(sc_type) \
            COMBO("sci", "Sidechain input", "SC input", expander_metadata::SC_TYPE_DFL, sc_type), \
            COMBO("scm", "Sidechain mode", "SC mode", expander_metadata::SC_MODE_DFL, exp_sc_modes), \
            CONTROL("sla", "Sidechain lookahead", "SC look", U_MSEC, expander_metadata::LOOKAHEAD), \
            SWITCH("scl", "Sidechain listen", "SC listen", 0.0f), \
            LOG_CONTROL("scr", "Sidechain reactivity", "SC react", U_MSEC, expander_metadata::REACTIVITY), \
            AMP_GAIN100("scp", "Sidechain preamp", "SC preamp", GAIN_AMP_0_DB), \
            COMBO("shpm", "High-pass filter mode", "HPF mode", 0, exp_filter_slope),      \
            LOG_CONTROL("shpf", "High-pass filter frequency", "HPF freq", U_HZ, expander_metadata::HPF),   \
            COMBO("slpm", "Low-pass filter mode", "LPF mode", 0, exp_filter_slope),      \
            LOG_CONTROL("slpf", "Low-pass filter frequency", "LPF freq", U_HZ, expander_metadata::LPF)

        #define EXP_STEREO_CHANNEL(id, label, alias, sc_type) \
            COMBO("sci" id, "Sidechain input" label, "SC input" alias, expander_metadata::SC_TYPE_DFL, sc_type), \
            COMBO("scm" id, "Sidechain mode" label, "SC mode" alias, expander_metadata::SC_MODE_DFL, exp_sc_modes), \
            CONTROL("sla" id, "Sidechain lookahead" label, "SC look" alias, U_MSEC, expander_metadata::LOOKAHEAD), \
            SWITCH("scl" id, "Sidechain listen" label, "SC listen" alias, 0.0f), \
            COMBO("scs" id, "Sidechain source" label, "SC source" alias, expander_metadata::SC_SOURCE_DFL, exp_sc_sources), \
            LOG_CONTROL("scr" id, "Sidechain reactivity" label, "SC react" alias, U_MSEC, expander_metadata::REACTIVITY), \
            AMP_GAIN100("scp" id, "Sidechain preamp" label, "SC preamp" alias, GAIN_AMP_0_DB), \
            COMBO("shpm" id, "High-pass filter mode" label, "HPF mode" alias, 0, exp_filter_slope),      \
            LOG_CONTROL("shpf" id, "High-pass filter frequency" label, "HPF freq" alias, U_HZ, expander_metadata::HPF),   \
            COMBO("slpm" id, "Low-pass filter mode" label, "LPF mode" alias, 0, exp_filter_slope),      \
            LOG_CONTROL("slpf" id, "Low-pass filter frequency" label, "LPF freq" alias, U_HZ, expander_metadata::LPF)

        #define EXP_CHANNEL(id, label, alias) \
            COMBO("em" id, "Expander mode" label, "Mode" alias, expander_metadata::EM_DEFAULT, exp_modes), \
            LOG_CONTROL("al" id, "Attack threshold" label, "Att lvl" alias, U_GAIN_AMP, expander_metadata::ATTACK_LVL), \
            LOG_CONTROL("at" id, "Attack time" label, "Att time" alias, U_MSEC, expander_metadata::ATTACK_TIME), \
            LOG_CONTROL("rrl" id, "Release threshold" label, "Rel lvl" alias, U_GAIN_AMP, expander_metadata::RELEASE_LVL), \
            LOG_CONTROL("rt" id, "Release time" label, "Rel time" alias, U_MSEC, expander_metadata::RELEASE_TIME), \
            CONTROL("hold" id, "Hold time" label, "Hold time" alias, U_MSEC, expander_metadata::HOLD_TIME), \
            LOG_CONTROL("er" id, "Ratio" label, "Ratio" alias, U_NONE, expander_metadata::RATIO), \
            LOG_CONTROL("kn" id, "Knee" label, "Knee" alias, U_GAIN_AMP, expander_metadata::KNEE), \
            LOG_CONTROL("mk" id, "Makeup gain" label, "Makeup" alias, U_GAIN_AMP, expander_metadata::MAKEUP), \
            AMP_GAIN10("cdr" id, "Dry gain" label, "Dry" alias, GAIN_AMP_M_INF_DB),     \
            AMP_GAIN10("cwt" id, "Wet gain" label, "Wet" alias, GAIN_AMP_0_DB), \
            PERCENTS("cdw" id, "Dry/Wet balance" label, "Dry/Wet" alias, 100.0f, 0.1f), \
            METER_OUT_GAIN("rl" id, "Release level" label, 20.0f), \
            MESH("ecg" id, "Expander curve graph" label, 2, expander_metadata::CURVE_MESH_SIZE)

        #define EXP_AUDIO_METER(id, label, alias) \
            SWITCH("slv" id, "Sidechain level visibility" label, "Show SC" alias, 1.0f), \
            SWITCH("elv" id, "Envelope level visibility" label, "Show Env" alias, 1.0f), \
            SWITCH("grv" id, "Gain reduction visibility" label, "Show Gain" alias, 1.0f), \
            SWITCH("ilv" id, "Input level visibility" label, "Show In" alias, 1.0f), \
            SWITCH("olv" id, "Output level visibility" label, "Show Out" alias, 1.0f), \
            MESH("scg" id, "Expander sidechain graph" label, 2, expander_metadata::TIME_MESH_SIZE), \
            MESH("evg" id, "Expander envelope graph" label, 2, expander_metadata::TIME_MESH_SIZE), \
            MESH("grg" id, "Expander gain reduciton" label, 2, expander_metadata::TIME_MESH_SIZE + 4), \
            MESH("icg" id, "Expander input" label, 2, expander_metadata::TIME_MESH_SIZE + 4), \
            MESH("ocg" id, "Expander output" label, 2, expander_metadata::TIME_MESH_SIZE), \
            METER_OUT_GAIN("slm" id, "Sidechain level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("clm" id, "Curve level meter" label, GAIN_AMP_P_36_DB), \
            METER_OUT_GAIN("elm" id, "Envelope level meter" label, GAIN_AMP_P_36_DB), \
            METER_GAIN_DFL("rlm" id, "Reduction level meter" label, GAIN_AMP_P_60_DB, GAIN_AMP_0_DB), \
            METER_GAIN("ilm" id, "Input level meter" label, GAIN_AMP_P_36_DB), \
            METER_GAIN("olm" id, "Output level meter" label, GAIN_AMP_P_36_DB)

        static const port_t expander_mono_ports[] =
        {
            PORTS_MONO_PLUGIN,
            EXP_SHM_LINK_MONO,
            EXP_COMMON,
            EXP_MONO_CHANNEL(exp_sc_type),
            EXP_CHANNEL("", "", ""),
            EXP_AUDIO_METER("", "", ""),

            PORTS_END
        };

        static const port_t expander_stereo_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            EXP_SHM_LINK_STEREO,
            EXP_COMMON,
            EXP_SPLIT_COMMON,
            EXP_STEREO_CHANNEL("", "", "", exp_sc_type),
            EXP_CHANNEL("", "", ""),
            EXP_AUDIO_METER("_l", " Left", " L"),
            EXP_AUDIO_METER("_r", " Right", " R"),

            PORTS_END
        };

        static const port_t expander_lr_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            EXP_SHM_LINK_STEREO,
            EXP_COMMON,
            EXP_STEREO_CHANNEL("_l", " Left", " L", exp_sc_type),
            EXP_STEREO_CHANNEL("_r", " Right", " R", exp_sc_type),
            EXP_CHANNEL("_l", " Left", " L"),
            EXP_CHANNEL("_r", " Right", " R"),
            EXP_AUDIO_METER("_l", " Left", " L"),
            EXP_AUDIO_METER("_r", " Right", " R"),

            PORTS_END
        };

        static const port_t expander_ms_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            EXP_SHM_LINK_STEREO,
            EXP_MS_COMMON,
            EXP_STEREO_CHANNEL("_m", " Mid", " M", exp_sc_type),
            EXP_STEREO_CHANNEL("_s", " Side", " S", exp_sc_type),
            EXP_CHANNEL("_m", " Mid", " M"),
            EXP_CHANNEL("_s", " Side", " S"),
            EXP_AUDIO_METER("_m", " Mid", " M"),
            EXP_AUDIO_METER("_s", " Side", " S"),

            PORTS_END
        };

        static const port_t sc_expander_mono_ports[] =
        {
            PORTS_MONO_PLUGIN,
            PORTS_MONO_SIDECHAIN,
            EXP_SHM_LINK_MONO,
            EXP_COMMON,
            EXP_MONO_CHANNEL(exp_extsc_type),
            EXP_CHANNEL("", "", ""),
            EXP_AUDIO_METER("", "", ""),

            PORTS_END
        };

        static const port_t sc_expander_stereo_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_STEREO_SIDECHAIN,
            EXP_SHM_LINK_STEREO,
            EXP_COMMON,
            EXP_SPLIT_COMMON,
            EXP_STEREO_CHANNEL("", "", "", exp_extsc_type),
            EXP_CHANNEL("", "", ""),
            EXP_AUDIO_METER("_l", " Left", " L"),
            EXP_AUDIO_METER("_r", " Right", " R"),

            PORTS_END
        };

        static const port_t sc_expander_lr_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_STEREO_SIDECHAIN,
            EXP_SHM_LINK_STEREO,
            EXP_COMMON,
            EXP_STEREO_CHANNEL("_l", " Left", " L", exp_extsc_type),
            EXP_STEREO_CHANNEL("_r", " Right", " R", exp_extsc_type),
            EXP_CHANNEL("_l", " Left", " L"),
            EXP_CHANNEL("_r", " Right", " R"),
            EXP_AUDIO_METER("_l", " Left", " L"),
            EXP_AUDIO_METER("_r", " Right", " R"),

            PORTS_END
        };

        static const port_t sc_expander_ms_ports[] =
        {
            PORTS_STEREO_PLUGIN,
            PORTS_STEREO_SIDECHAIN,
            EXP_SHM_LINK_STEREO,
            EXP_MS_COMMON,
            EXP_STEREO_CHANNEL("_m", " Mid", " M", exp_extsc_type),
            EXP_STEREO_CHANNEL("_s", " Side", " S", exp_extsc_type),
            EXP_CHANNEL("_m", " Mid", " M"),
            EXP_CHANNEL("_s", " Side", " S"),
            EXP_AUDIO_METER("_m", " Mid", " M"),
            EXP_AUDIO_METER("_s", " Side", " S"),

            PORTS_END
        };

        const meta::bundle_t expander_bundle =
        {
            "expander",
            "Expander",
            B_DYNAMICS,
            "p6otNrilF0U",
            "This plugin performs increasing of dynamic range of input signal.\nFlexible sidechain-control configuration provided. Both downward\nand upward modes are available. Also additional dry/wet control\nallows one to mix processed and unprocessed signal together."
        };

        // Expander
        const meta::plugin_t  expander_mono =
        {
            "Expander Mono",
            "Expander Mono",
            "Expander Mono",
            "E1M",
            &developers::v_sadovnikov,
            "expander_mono",
            {
                LSP_LV2_URI("expander_mono"),
                LSP_LV2UI_URI("expander_mono"),
                "tddq",
                LSP_VST3_UID("e1m     tddq"),
                LSP_VST3UI_UID("e1m     tddq"),
                LSP_LADSPA_EXPANDER_BASE + 0,
                LSP_LADSPA_URI("expander_mono"),
                LSP_CLAP_URI("expander_mono"),
                LSP_GST_UID("expander_mono"),
            },
            LSP_PLUGINS_EXPANDER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            expander_mono_ports,
            "dynamics/expander/single.xml",
            NULL,
            mono_plugin_port_groups,
            &expander_bundle
        };

        const meta::plugin_t  expander_stereo =
        {
            "Expander Stereo",
            "Expander Stereo",
            "Expander Stereo",
            "E1S",
            &developers::v_sadovnikov,
            "expander_stereo",
            {
                LSP_LV2_URI("expander_stereo"),
                LSP_LV2UI_URI("expander_stereo"),
                "au0f",
                LSP_VST3_UID("e1s     au0f"),
                LSP_VST3UI_UID("e1s     au0f"),
                LSP_LADSPA_EXPANDER_BASE + 1,
                LSP_LADSPA_URI("expander_stereo"),
                LSP_CLAP_URI("expander_stereo"),
                LSP_GST_UID("expander_stereo"),
            },
            LSP_PLUGINS_EXPANDER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            expander_stereo_ports,
            "dynamics/expander/single.xml",
            NULL,
            stereo_plugin_port_groups,
            &expander_bundle
        };

        const meta::plugin_t  expander_lr =
        {
            "Expander LeftRight",
            "Expander LeftRight",
            "Expander L/R",
            "E1LR",
            &developers::v_sadovnikov,
            "expander_lr",
            {
                LSP_LV2_URI("expander_lr"),
                LSP_LV2UI_URI("expander_lr"),
                "zvsf",
                LSP_VST3_UID("e1lr    zvsf"),
                LSP_VST3UI_UID("e1lr    zvsf"),
                LSP_LADSPA_EXPANDER_BASE + 2,
                LSP_LADSPA_URI("expander_lr"),
                LSP_CLAP_URI("expander_lr"),
                LSP_GST_UID("expander_lr"),
            },
            LSP_PLUGINS_EXPANDER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            expander_lr_ports,
            "dynamics/expander/single.xml",
            NULL,
            stereo_plugin_port_groups,
            &expander_bundle
        };

        const meta::plugin_t  expander_ms =
        {
            "Expander MidSide",
            "Expander MidSide",
            "Expander M/S",
            "E1MS",
            &developers::v_sadovnikov,
            "expander_ms",
            {
                LSP_LV2_URI("expander_ms"),
                LSP_LV2UI_URI("expander_ms"),
                "ebhk",
                LSP_VST3_UID("e1ms    ebhk"),
                LSP_VST3UI_UID("e1ms    ebhk"),
                LSP_LADSPA_EXPANDER_BASE + 3,
                LSP_LADSPA_URI("expander_ms"),
                LSP_CLAP_URI("expander_ms"),
                LSP_GST_UID("expander_ms"),
            },
            LSP_PLUGINS_EXPANDER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            expander_ms_ports,
            "dynamics/expander/single.xml",
            NULL,
            stereo_plugin_port_groups,
            &expander_bundle
        };

        // Sidechain expander
        const meta::plugin_t  sc_expander_mono =
        {
            "Sidechain-Expander Mono",
            "Sidechain Expander Mono",
            "SC Expander Mono",
            "SCE1M",
            &developers::v_sadovnikov,
            "sc_expander_mono",
            {
                LSP_LV2_URI("sc_expander_mono"),
                LSP_LV2UI_URI("sc_expander_mono"),
                "utul",
                LSP_VST3_UID("sce1m   utul"),
                LSP_VST3UI_UID("sce1m   utul"),
                LSP_LADSPA_EXPANDER_BASE + 4,
                LSP_LADSPA_URI("sc_expander_mono"),
                LSP_CLAP_URI("sc_expander_mono"),
                LSP_GST_UID("sc_expander_mono"),
            },
            LSP_PLUGINS_EXPANDER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            sc_expander_mono_ports,
            "dynamics/expander/single.xml",
            NULL,
            mono_plugin_sidechain_port_groups,
            &expander_bundle
        };

        const meta::plugin_t  sc_expander_stereo =
        {
            "Sidechain-Expander Stereo",
            "Sidechain Expander Stereo",
            "SC Expander Stereo",
            "SCE1S",
            &developers::v_sadovnikov,
            "sc_expander_stereo",
            {
                LSP_LV2_URI("sc_expander_stereo"),
                LSP_LV2UI_URI("sc_expander_stereo"),
                "lwqv",
                LSP_VST3_UID("sce1s   lwqv"),
                LSP_VST3UI_UID("sce1s   lwqv"),
                LSP_LADSPA_EXPANDER_BASE + 5,
                LSP_LADSPA_URI("sc_expander_stereo"),
                LSP_CLAP_URI("sc_expander_stereo"),
                LSP_GST_UID("sc_expander_stereo"),
            },
            LSP_PLUGINS_EXPANDER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            sc_expander_stereo_ports,
            "dynamics/expander/single.xml",
            NULL,
            stereo_plugin_sidechain_port_groups,
            &expander_bundle
        };

        const meta::plugin_t  sc_expander_lr =
        {
            "Sidechain-Expander LeftRight",
            "Sidechain Expander LeftRight",
            "SC Expander L/R",
            "SCE1LR",
            &developers::v_sadovnikov,
            "sc_expander_lr",
            {
                LSP_LV2_URI("sc_expander_lr"),
                LSP_LV2UI_URI("sc_expander_lr"),
                "pnvw",
                LSP_VST3_UID("sce1lr  pnvw"),
                LSP_VST3UI_UID("sce1lr  pnvw"),
                LSP_LADSPA_EXPANDER_BASE + 6,
                LSP_LADSPA_URI("sc_expander_lr"),
                LSP_CLAP_URI("sc_expander_lr"),
                LSP_GST_UID("sc_expander_lr"),
            },
            LSP_PLUGINS_EXPANDER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            sc_expander_lr_ports,
            "dynamics/expander/single.xml",
            NULL,
            stereo_plugin_sidechain_port_groups,
            &expander_bundle
        };

        const meta::plugin_t  sc_expander_ms =
        {
            "Sidechain-Expander MidSide",
            "Sidechain Expander MidSide",
            "SC Expander M/S",
            "SCE1MS",
            &developers::v_sadovnikov,
            "sc_expander_ms",
            {
                LSP_LV2_URI("sc_expander_ms"),
                LSP_LV2UI_URI("sc_expander_ms"),
                "zc0d",
                LSP_VST3_UID("sce1ms  zc0d"),
                LSP_VST3UI_UID("sce1ms  zc0d"),
                LSP_LADSPA_EXPANDER_BASE + 7,
                LSP_LADSPA_URI("sc_expander_ms"),
                LSP_CLAP_URI("sc_expander_ms"),
                LSP_GST_UID("sc_expander_ms"),
            },
            LSP_PLUGINS_EXPANDER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            sc_expander_ms_ports,
            "dynamics/expander/single.xml",
            NULL,
            stereo_plugin_sidechain_port_groups,
            &expander_bundle
        };
    } /* namespace meta */
} /* namespace lsp */
