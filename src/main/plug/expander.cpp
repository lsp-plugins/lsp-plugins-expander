/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-expander
 * Created on: 3 авг. 2024 г.
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

#include <private/plugins/expander.h>

#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/core/AudioBuffer.h>
#include <lsp-plug.in/shared/id_colors.h>
#include <lsp-plug.in/shared/debug.h>

#define EXP_BUF_SIZE            0x1000

namespace lsp
{
    namespace plugins
    {
        //-------------------------------------------------------------------------
        // Plugin factory
        typedef struct plugin_settings_t
        {
            const meta::plugin_t   *metadata;
            bool                    sc;
            uint8_t                 mode;
        } plugin_settings_t;

        static const meta::plugin_t *plugins[] =
        {
            &meta::expander_mono,
            &meta::expander_stereo,
            &meta::expander_lr,
            &meta::expander_ms,
            &meta::sc_expander_mono,
            &meta::sc_expander_stereo,
            &meta::sc_expander_lr,
            &meta::sc_expander_ms
        };

        static const plugin_settings_t plugin_settings[] =
        {
            { &meta::expander_mono,       false, expander::EM_MONO          },
            { &meta::expander_stereo,     false, expander::EM_STEREO        },
            { &meta::expander_lr,         false, expander::EM_LR            },
            { &meta::expander_ms,         false, expander::EM_MS            },
            { &meta::sc_expander_mono,    true,  expander::EM_MONO          },
            { &meta::sc_expander_stereo,  true,  expander::EM_STEREO        },
            { &meta::sc_expander_lr,      true,  expander::EM_LR            },
            { &meta::sc_expander_ms,      true,  expander::EM_MS            },

            { NULL, 0, false }
        };

        static plug::Module *plugin_factory(const meta::plugin_t *meta)
        {
            for (const plugin_settings_t *s = plugin_settings; s->metadata != NULL; ++s)
                if (s->metadata == meta)
                    return new expander(s->metadata, s->sc, s->mode);
            return NULL;
        }

        static plug::Factory factory(plugin_factory, plugins, 8);

        //-------------------------------------------------------------------------
        expander::expander(const meta::plugin_t *metadata, bool sc, size_t mode): plug::Module(metadata)
        {
            nMode           = mode;
            bSidechain      = sc;
            vChannels       = NULL;
            vCurve          = NULL;
            vTime           = NULL;
            vEmptyBuffer    = NULL;
            bPause          = false;
            bClear          = false;
            bMSListen       = false;
            bStereoSplit    = false;
            fInGain         = 1.0f;
            bUISync         = true;

            pBypass         = NULL;
            pInGain         = NULL;
            pOutGain        = NULL;
            pPause          = NULL;
            pClear          = NULL;
            pMSListen       = NULL;
            pStereoSplit    = NULL;
            pScSpSource     = NULL;

            pData           = NULL;
            pIDisplay       = NULL;
        }

        expander::~expander()
        {
            do_destroy();
        }

        void expander::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            plug::Module::init(wrapper, ports);
            size_t channels = (nMode == EM_MONO) ? 1 : 2;

            // Allocate temporary buffers
            size_t channel_size     = align_size(sizeof(channel_t) * channels, DEFAULT_ALIGN);
            size_t buf_size         = EXP_BUF_SIZE * sizeof(float);
            size_t curve_size       = (meta::expander_metadata::CURVE_MESH_SIZE) * sizeof(float);
            size_t history_size     = (meta::expander_metadata::TIME_MESH_SIZE) * sizeof(float);
            size_t allocate         = channel_size +
                                      buf_size +
                                      buf_size * channels * 5 +
                                      curve_size +
                                      history_size;

            uint8_t *ptr            = alloc_aligned<uint8_t>(pData, allocate);
            if (ptr == NULL)
                return;

            vChannels               = advance_ptr_bytes<channel_t>(ptr, channel_size);
            vCurve                  = advance_ptr_bytes<float>(ptr, curve_size);
            vTime                   = advance_ptr_bytes<float>(ptr, history_size);
            vEmptyBuffer            = advance_ptr_bytes<float>(ptr, buf_size);

            // Initialize channels
            for (size_t i=0; i<channels; ++i)
            {
                // Construct the channel
                channel_t *c = &vChannels[i];
                c->sBypass.construct();
                c->sSC.construct();
                c->sSCEq.construct();
                c->sExp.construct();
                c->sLaDelay.construct();
                c->sInDelay.construct();
                c->sOutDelay.construct();
                c->sDryDelay.construct();
                for (size_t j=0; j<G_TOTAL; ++j)
                    c->sGraph[j].construct();

                // Initialize the channel
                if (!c->sSC.init(channels, meta::expander_metadata::REACTIVITY_MAX))
                    return;
                if (!c->sSCEq.init(2, 12))
                    return;
                c->sSCEq.set_mode(dspu::EQM_IIR);
                c->sSC.set_pre_equalizer(&c->sSCEq);

                c->vIn              = advance_ptr_bytes<float>(ptr, buf_size);
                c->vOut             = advance_ptr_bytes<float>(ptr, buf_size);
                c->vSc              = advance_ptr_bytes<float>(ptr, buf_size);
                c->vEnv             = advance_ptr_bytes<float>(ptr, buf_size);
                c->vGain            = advance_ptr_bytes<float>(ptr, buf_size);
                c->bScListen        = false;
                c->nSync            = S_ALL;
                c->nScType          = SCT_INTERNAL;
                c->fMakeup          = 1.0f;
                c->fDryGain         = 1.0f;
                c->fWetGain         = 0.0f;
                c->fDotIn           = 0.0f;
                c->fDotOut          = 0.0f;

                c->pIn              = NULL;
                c->pOut             = NULL;
                c->pSC              = NULL;
                c->pShmIn           = NULL;

                for (size_t j=0; j<G_TOTAL; ++j)
                    c->pGraph[j]        = NULL;

                for (size_t j=0; j<M_TOTAL; ++j)
                    c->pMeter[j]        = NULL;

                c->pScType          = NULL;
                c->pScMode          = NULL;
                c->pScLookahead     = NULL;
                c->pScListen        = NULL;
                c->pScSource        = NULL;
                c->pScReactivity    = NULL;
                c->pScPreamp        = NULL;
                c->pScHpfMode       = NULL;
                c->pScHpfFreq       = NULL;
                c->pScLpfMode       = NULL;
                c->pScLpfFreq       = NULL;

                c->pMode            = NULL;
                c->pAttackLvl       = NULL;
                c->pReleaseLvl      = NULL;
                c->pAttackTime      = NULL;
                c->pReleaseTime     = NULL;
                c->pHoldTime        = NULL;
                c->pRatio           = NULL;
                c->pKnee            = NULL;
                c->pMakeup          = NULL;
                c->pDryGain         = NULL;
                c->pWetGain         = NULL;
                c->pDryWet          = NULL;
                c->pCurve           = NULL;
                c->pReleaseOut      = NULL;
            }

            lsp_assert(ptr < &pData[allocate]);

            // Bind ports
            size_t port_id              = 0;

            // Input ports
            lsp_trace("Binding input ports");
            for (size_t i=0; i<channels; ++i)
                BIND_PORT(vChannels[i].pIn);

            // Output ports
            lsp_trace("Binding output ports");
            for (size_t i=0; i<channels; ++i)
                BIND_PORT(vChannels[i].pOut);

            // Sidechain ports
            if (bSidechain)
            {
                lsp_trace("Binding sidechain ports");
                for (size_t i=0; i<channels; ++i)
                    BIND_PORT(vChannels[i].pSC);
            }

            // Shared memory link
            lsp_trace("Binding shared memory link");
            SKIP_PORT("Shared memory link name");
            for (size_t i=0; i<channels; ++i)
                BIND_PORT(vChannels[i].pShmIn);

            // Common ports
            lsp_trace("Binding common ports");
            BIND_PORT(pBypass);
            BIND_PORT(pInGain);
            BIND_PORT(pOutGain);
            BIND_PORT(pPause);
            BIND_PORT(pClear);
            if (nMode == EM_MS)
                BIND_PORT(pMSListen);
            if (nMode == EM_STEREO)
            {
                BIND_PORT(pStereoSplit);
                BIND_PORT(pScSpSource);
            }

            // Sidechain ports
            lsp_trace("Binding sidechain ports");
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c = &vChannels[i];

                if ((i > 0) && (nMode == EM_STEREO))
                {
                    channel_t *sc       = &vChannels[0];
                    c->pScType          = sc->pScType;
                    c->pScSource        = sc->pScSource;
                    c->pScMode          = sc->pScMode;
                    c->pScLookahead     = sc->pScLookahead;
                    c->pScListen        = sc->pScListen;
                    c->pScReactivity    = sc->pScReactivity;
                    c->pScPreamp        = sc->pScPreamp;
                    c->pScHpfMode       = sc->pScHpfMode;
                    c->pScHpfFreq       = sc->pScHpfFreq;
                    c->pScLpfMode       = sc->pScLpfMode;
                    c->pScLpfFreq       = sc->pScLpfFreq;
                }
                else
                {
                    BIND_PORT(c->pScType);
                    BIND_PORT(c->pScMode);
                    BIND_PORT(c->pScLookahead);
                    BIND_PORT(c->pScListen);
                    if (nMode != EM_MONO)
                        BIND_PORT(c->pScSource);
                    BIND_PORT(c->pScReactivity);
                    BIND_PORT(c->pScPreamp);
                    BIND_PORT(c->pScHpfMode);
                    BIND_PORT(c->pScHpfFreq);
                    BIND_PORT(c->pScLpfMode);
                    BIND_PORT(c->pScLpfFreq);
                }
            }

            // Expander ports
            lsp_trace("Binding expander ports");
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c = &vChannels[i];

                if ((i > 0) && (nMode == EM_STEREO))
                {
                    channel_t *sc       = &vChannels[0];

                    c->pMode            = sc->pMode;
                    c->pAttackLvl       = sc->pAttackLvl;
                    c->pAttackTime      = sc->pAttackTime;
                    c->pReleaseLvl      = sc->pReleaseLvl;
                    c->pReleaseTime     = sc->pReleaseTime;
                    c->pHoldTime        = sc->pHoldTime;
                    c->pRatio           = sc->pRatio;
                    c->pKnee            = sc->pKnee;
                    c->pMakeup          = sc->pMakeup;
                    c->pDryGain         = sc->pDryGain;
                    c->pWetGain         = sc->pWetGain;
                    c->pDryWet          = sc->pDryWet;
                }
                else
                {
                    BIND_PORT(c->pMode);
                    BIND_PORT(c->pAttackLvl);
                    BIND_PORT(c->pAttackTime);
                    BIND_PORT(c->pReleaseLvl);
                    BIND_PORT(c->pReleaseTime);
                    BIND_PORT(c->pHoldTime);
                    BIND_PORT(c->pRatio);
                    BIND_PORT(c->pKnee);
                    BIND_PORT(c->pMakeup);
                    BIND_PORT(c->pDryGain);
                    BIND_PORT(c->pWetGain);
                    BIND_PORT(c->pDryWet);
                    BIND_PORT(c->pReleaseOut);
                    BIND_PORT(c->pCurve);
                }
            }

            // Bind history
            lsp_trace("Binding history ports");
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c = &vChannels[i];

                // Skip meters visibility controls
                SKIP_PORT("Sidechain switch");
                SKIP_PORT("Envelope switch");
                SKIP_PORT("Gain reduction switch");
                SKIP_PORT("Input switch");
                SKIP_PORT("Output switch");

                // Bind ports
                BIND_PORT(c->pGraph[G_SC]);
                BIND_PORT(c->pGraph[G_ENV]);
                BIND_PORT(c->pGraph[G_GAIN]);
                BIND_PORT(c->pGraph[G_IN]);
                BIND_PORT(c->pGraph[G_OUT]);
                BIND_PORT(c->pMeter[M_SC]);
                BIND_PORT(c->pMeter[M_CURVE]);
                BIND_PORT(c->pMeter[M_ENV]);
                BIND_PORT(c->pMeter[M_GAIN]);
                BIND_PORT(c->pMeter[M_IN]);
                BIND_PORT(c->pMeter[M_OUT]);
            }

            dsp::fill_zero(vEmptyBuffer, EXP_BUF_SIZE);

            // Initialize curve (logarithmic) in range of -72 .. +24 db
            float delta = (meta::expander_metadata::CURVE_DB_MAX - meta::expander_metadata::CURVE_DB_MIN) / (meta::expander_metadata::CURVE_MESH_SIZE-1);
            for (size_t i=0; i<meta::expander_metadata::CURVE_MESH_SIZE; ++i)
                vCurve[i]   = dspu::db_to_gain(meta::expander_metadata::CURVE_DB_MIN + delta * i);

            // Initialize time points
            delta       = meta::expander_metadata::TIME_HISTORY_MAX / (meta::expander_metadata::TIME_MESH_SIZE - 1);
            for (size_t i=0; i<meta::expander_metadata::TIME_MESH_SIZE; ++i)
                vTime[i]    = meta::expander_metadata::TIME_HISTORY_MAX - i*delta;
        }

        void expander::destroy()
        {
            Module::destroy();
            do_destroy();
        }

        void expander::do_destroy()
        {
            if (vChannels != NULL)
            {
                size_t channels = (nMode == EM_MONO) ? 1 : 2;
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c = &vChannels[i];

                    c->sBypass.destroy();
                    c->sSC.destroy();
                    c->sSCEq.destroy();
                    c->sExp.destroy();
                    c->sLaDelay.destroy();
                    c->sInDelay.destroy();
                    c->sOutDelay.destroy();
                    c->sDryDelay.destroy();
                    for (size_t j=0; j<G_TOTAL; ++j)
                        c->sGraph[j].destroy();
                }

                vChannels = NULL;
            }

            if (pData != NULL)
            {
                free_aligned(pData);
                pData = NULL;
            }

            if (pIDisplay != NULL)
            {
                pIDisplay->destroy();
                pIDisplay   = NULL;
            }
        }

        void expander::update_sample_rate(long sr)
        {
            size_t samples_per_dot  = dspu::seconds_to_samples(sr, meta::expander_metadata::TIME_HISTORY_MAX / meta::expander_metadata::TIME_MESH_SIZE);
            size_t channels = (nMode == EM_MONO) ? 1 : 2;
            size_t max_delay    = dspu::millis_to_samples(fSampleRate, meta::expander_metadata::LOOKAHEAD_MAX);

            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c = &vChannels[i];
                c->sBypass.init(sr);
                c->sExp.set_sample_rate(sr);
                c->sSC.set_sample_rate(sr);
                c->sSCEq.set_sample_rate(sr);
                c->sLaDelay.init(max_delay);
                c->sInDelay.init(max_delay);
                c->sOutDelay.init(max_delay);
                c->sDryDelay.init(max_delay);

                for (size_t j=0; j<G_TOTAL; ++j)
                    c->sGraph[j].init(meta::expander_metadata::TIME_MESH_SIZE, samples_per_dot);
                c->sGraph[G_GAIN].fill(1.0f);
            }
        }

        dspu::sidechain_source_t expander::decode_sidechain_source(int source, bool split, size_t channel)
        {
            if (!split)
            {
                switch (source)
                {
                    case 0: return dspu::SCS_MIDDLE;
                    case 1: return dspu::SCS_SIDE;
                    case 2: return dspu::SCS_LEFT;
                    case 3: return dspu::SCS_RIGHT;
                    case 4: return dspu::SCS_AMIN;
                    case 5: return dspu::SCS_AMAX;
                    default: break;
                }
            }

            if (channel == 0)
            {
                switch (source)
                {
                    case 0: return dspu::SCS_LEFT;
                    case 1: return dspu::SCS_RIGHT;
                    case 2: return dspu::SCS_MIDDLE;
                    case 3: return dspu::SCS_SIDE;
                    case 4: return dspu::SCS_AMIN;
                    case 5: return dspu::SCS_AMAX;
                    default: break;
                }
            }
            else
            {
                switch (source)
                {
                    case 0: return dspu::SCS_RIGHT;
                    case 1: return dspu::SCS_LEFT;
                    case 2: return dspu::SCS_SIDE;
                    case 3: return dspu::SCS_MIDDLE;
                    case 4: return dspu::SCS_AMIN;
                    case 5: return dspu::SCS_AMAX;
                    default: break;
                }
            }

            return dspu::SCS_MIDDLE;
        }

        uint32_t expander::decode_sidechain_type(uint32_t sc) const
        {
            if (bSidechain)
            {
                switch (sc)
                {
                    case 0: return SCT_INTERNAL;
                    case 1: return SCT_EXTERNAL;
                    case 2: return SCT_LINK;
                    default: break;
                }
            }
            else
            {
                switch (sc)
                {
                    case 0: return SCT_INTERNAL;
                    case 1: return SCT_LINK;
                    default: break;
                }
            }

            return SCT_INTERNAL;
        }

        inline bool expander::use_sidechain(const channel_t & c)
        {
            switch (c.nScType)
            {
                case SCT_EXTERNAL:
                case SCT_LINK:
                    return true;
                default:
                    break;
            }
            return false;
        }

        inline float *expander::select_buffer(const channel_t & c, float *in, float *sc, float *shm)
        {
            switch (c.nScType)
            {
                case SCT_EXTERNAL: return (sc != NULL) ? sc : vEmptyBuffer;
                case SCT_LINK: return (shm != NULL) ? shm : vEmptyBuffer;
                default: break;
            }

            return in;
        }

        void expander::update_settings()
        {
            dspu::filter_params_t fp;
            size_t channels = (nMode == EM_MONO) ? 1 : 2;
            bool bypass     = pBypass->value() >= 0.5f;

            // Global parameters
            bPause          = pPause->value() >= 0.5f;
            bClear          = pClear->value() >= 0.5f;
            bMSListen       = (pMSListen != NULL) ? pMSListen->value() >= 0.5f : false;
            bStereoSplit    = (pStereoSplit != NULL) ? pStereoSplit->value() >= 0.5f : false;
            fInGain         = pInGain->value();
            float out_gain  = pOutGain->value();
            size_t latency  = 0;

            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];
                plug::IPort *sc = (bStereoSplit) ? pScSpSource : c->pScSource;
                size_t sc_src   = (sc != NULL) ? sc->value() : dspu::SCS_MIDDLE;

                // Update bypass settings
                c->sBypass.set_bypass(bypass);

                // Update sidechain settings
                c->nScType      = decode_sidechain_type(c->pScType->value());
                c->bScListen    = c->pScListen->value() >= 0.5f;

                c->sSC.set_gain(c->pScPreamp->value());
                c->sSC.set_mode((c->pScMode != NULL) ? c->pScMode->value() : dspu::SCM_RMS);
                c->sSC.set_source(decode_sidechain_source(sc_src, bStereoSplit, i));
                c->sSC.set_reactivity(c->pScReactivity->value());
                c->sSC.set_stereo_mode(((nMode == EM_MS) && (!use_sidechain(*c))) ? dspu::SCSM_MIDSIDE : dspu::SCSM_STEREO);

                // Setup hi-pass filter for sidechain
                size_t hp_slope = c->pScHpfMode->value() * 2;
                fp.nType        = (hp_slope > 0) ? dspu::FLT_BT_BWC_HIPASS : dspu::FLT_NONE;
                fp.fFreq        = c->pScHpfFreq->value();
                fp.fFreq2       = fp.fFreq;
                fp.fGain        = 1.0f;
                fp.nSlope       = hp_slope;
                fp.fQuality     = 0.0f;
                c->sSCEq.set_params(0, &fp);

                // Setup low-pass filter for sidechain
                size_t lp_slope = c->pScLpfMode->value() * 2;
                fp.nType        = (lp_slope > 0) ? dspu::FLT_BT_BWC_LOPASS : dspu::FLT_NONE;
                fp.fFreq        = c->pScLpfFreq->value();
                fp.fFreq2       = fp.fFreq;
                fp.fGain        = 1.0f;
                fp.nSlope       = lp_slope;
                fp.fQuality     = 0.0f;
                c->sSCEq.set_params(1, &fp);

                // Update delay
                size_t delay    = dspu::millis_to_samples(fSampleRate, (c->pScLookahead != NULL) ? c->pScLookahead->value() : 0);
                c->sLaDelay.set_delay(delay);
                latency         = lsp_max(latency, delay);

                // Update expander settings
                float attack    = c->pAttackLvl->value();
                float release   = c->pReleaseLvl->value() * attack;
                bool upward     = c->pMode->value() >= 0.5f;

                c->sExp.set_threshold(attack, release);
                c->sExp.set_timings(c->pAttackTime->value(), c->pReleaseTime->value());
                c->sExp.set_hold(c->pHoldTime->value());
                c->sExp.set_ratio(c->pRatio->value());
                c->sExp.set_knee(c->pKnee->value());
                c->sExp.set_mode((upward) ? dspu::EM_UPWARD : dspu::EM_DOWNWARD);
                if (c->pReleaseOut != NULL)
                    c->pReleaseOut->set_value(release);
                c->sGraph[G_GAIN].set_method((upward) ? dspu::MM_MAXIMUM : dspu::MM_MINIMUM);

                // Check modification flag
                if (c->sExp.modified())
                {
                    c->sExp.update_settings();
                    c->nSync           |= S_CURVE;
                }

                // Update gains
                const float makeup      = c->pMakeup->value();
                const float dry_gain    = c->pDryGain->value();
                const float wet_gain    = c->pWetGain->value() * makeup;
                const float drywet      = c->pDryWet->value() * 0.01f;

                c->fDryGain         = (dry_gain * drywet + 1.0f - drywet) * out_gain;
                c->fWetGain         = wet_gain * drywet * out_gain;

                if (c->fMakeup != makeup)
                {
                    c->fMakeup          = makeup;
                    c->nSync           |= S_CURVE;
                }
            }

            // Tune compensation delays
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];
                c->sInDelay.set_delay(latency);
                c->sOutDelay.set_delay(latency - c->sLaDelay.get_delay());
                c->sDryDelay.set_delay(latency);
            }

            // Report latency
            set_latency(latency);
        }

        void expander::ui_activated()
        {
            size_t channels     = (nMode == EM_MONO) ? 1 : 2;
            for (size_t i=0; i<channels; ++i)
                vChannels[i].nSync     = S_CURVE;
            bUISync             = true;
        }

        void expander::process(size_t samples)
        {
            size_t channels = (nMode == EM_MONO) ? 1 : 2;

            float *in_buf[2];   // Input buffer
            float *out_buf[2];  // Output buffer
            float *sc_buf[2];   // Sidechain source
            float *shm_buf[2];  // Shared memory source
            const float *in[2]; // Buffet to pass to sidechain

            // Prepare audio channels
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c        = &vChannels[i];

                // Initialize pointers
                in_buf[i]           = c->pIn->buffer<float>();
                out_buf[i]          = c->pOut->buffer<float>();
                sc_buf[i]           = (c->pSC != NULL) ? c->pSC->buffer<float>() : in_buf[i];
                shm_buf[i]          = NULL;

                core::AudioBuffer *buf = (c->pShmIn != NULL) ? c->pShmIn->buffer<core::AudioBuffer>() : NULL;
                if ((buf != NULL) && (buf->active()))
                    shm_buf[i]          = buf->buffer();
            }

            // Perform expansion
            size_t left = samples;
            while (left > 0)
            {
                // Detemine number of samples to process
                size_t to_process = (left > EXP_BUF_SIZE) ? EXP_BUF_SIZE : left;

                // Prepare audio channels
                if (nMode == EM_MONO)
                    dsp::mul_k3(vChannels[0].vIn, in_buf[0], fInGain, to_process);
                else if (nMode == EM_MS)
                {
                    dsp::lr_to_ms(vChannels[0].vIn, vChannels[1].vIn, in_buf[0], in_buf[1], to_process);
                    dsp::mul_k2(vChannels[0].vIn, fInGain, to_process);
                    dsp::mul_k2(vChannels[1].vIn, fInGain, to_process);
                }
                else
                {
                    dsp::mul_k3(vChannels[0].vIn, in_buf[0], fInGain, to_process);
                    dsp::mul_k3(vChannels[1].vIn, in_buf[1], fInGain, to_process);
                }

                // Perform sidechain processing
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c        = &vChannels[i];

                    // Update input graph
                    c->sGraph[G_IN].process(c->vIn, to_process);
                    c->pMeter[M_IN]->set_value(dsp::abs_max(c->vIn, to_process));

                    // Do expansion
                    in[0]   = select_buffer(*c, vChannels[0].vIn, sc_buf[0], shm_buf[0]);
                    if (channels > 1)
                        in[1]   = select_buffer(*c, vChannels[1].vIn, sc_buf[1], shm_buf[1]);
                    c->sSC.process(c->vSc, in, to_process);
                    c->sExp.process(c->vGain, c->vEnv, c->vSc, to_process);
                }

                // Apply gain to each channel and process meters
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c        = &vChannels[i];

                    // Add delay to original signal and apply gain
                    c->sLaDelay.process(c->vOut, c->vIn, c->vGain, to_process);
                    c->sInDelay.process(c->vIn, c->vIn, to_process);
                    c->sOutDelay.process(c->vOut, c->vOut, to_process);

                    // Process graph outputs
                    c->sGraph[G_SC].process(c->vSc, to_process);                        // Sidechain signal
                    c->pMeter[M_SC]->set_value(dsp::abs_max(c->vSc, to_process));

                    c->sGraph[G_GAIN].process(c->vGain, to_process);                    // Gain reduction signal
                    c->pMeter[M_GAIN]->set_value(dsp::abs_max(c->vGain, to_process));

                    c->sGraph[G_ENV].process(c->vEnv, to_process);                      // Envelope signal
                    c->pMeter[M_ENV]->set_value(dsp::abs_max(c->vEnv, to_process));
                }

                // Form output signal
                if (nMode == EM_MS)
                {
                    channel_t *cm       = &vChannels[0];
                    channel_t *cs       = &vChannels[1];

                    dsp::mix2(cm->vOut, cm->vIn, cm->fWetGain, cm->fDryGain, to_process);
                    dsp::mix2(cs->vOut, cs->vIn, cs->fWetGain, cs->fDryGain, to_process);

                    cm->sGraph[G_OUT].process(cm->vOut, to_process);
                    cm->pMeter[M_OUT]->set_value(dsp::abs_max(cm->vOut, to_process));
                    cs->sGraph[G_OUT].process(cs->vOut, to_process);
                    cs->pMeter[M_OUT]->set_value(dsp::abs_max(cs->vOut, to_process));

                    if (!bMSListen)
                        dsp::ms_to_lr(cm->vOut, cs->vOut, cm->vOut, cs->vOut, to_process);
                    if (cm->bScListen)
                        dsp::copy(cm->vOut, cm->vSc, to_process);
                    if (cs->bScListen)
                        dsp::copy(cs->vOut, cs->vSc, to_process);
                }
                else
                {
                    for (size_t i=0; i<channels; ++i)
                    {
                        // Mix dry/wet signal or copy sidechain signal
                        channel_t *c        = &vChannels[i];
                        if (c->bScListen)
                            dsp::copy(c->vOut, c->vSc, to_process);
                        else
                            dsp::mix2(c->vOut, c->vIn, c->fWetGain, c->fDryGain, to_process);

                        c->sGraph[G_OUT].process(c->vOut, to_process);                      // Output signal
                        c->pMeter[M_OUT]->set_value(dsp::abs_max(c->vOut, to_process));
                    }
                }

                // Final metering
                for (size_t i=0; i<channels; ++i)
                {
                    // Apply bypass
                    channel_t *c        = &vChannels[i];
                    c->sDryDelay.process(c->vIn, in_buf[i], to_process);
                    c->sBypass.process(out_buf[i], c->vIn, c->vOut, to_process);

                    in_buf[i]          += to_process;
                    out_buf[i]         += to_process;
                    if (sc_buf[i] != NULL)
                        sc_buf[i]          += to_process;
                    if (shm_buf[i] != NULL)
                        shm_buf[i]         += to_process;
                }

                left       -= to_process;
            }

            if ((!bPause) || (bClear) || (bUISync))
            {
                // Process mesh requests
                for (size_t i=0; i<channels; ++i)
                {
                    // Get channel
                    channel_t *c        = &vChannels[i];

                    for (size_t j=0; j<G_TOTAL; ++j)
                    {
                        // Check that port is bound
                        if (c->pGraph[j] == NULL)
                            continue;

                        // Clear data if requested
                        if (bClear)
                            dsp::fill_zero(c->sGraph[j].data(), meta::expander_metadata::TIME_MESH_SIZE);

                        // Get mesh
                        plug::mesh_t *mesh    = c->pGraph[j]->buffer<plug::mesh_t>();
                        if ((mesh != NULL) && (mesh->isEmpty()))
                        {
                            // Fill mesh with new values
                            if (j == G_IN)
                            {
                                float *x = mesh->pvData[0];
                                float *y = mesh->pvData[1];

                                dsp::copy(&x[1], vTime, meta::expander_metadata::TIME_MESH_SIZE);
                                dsp::copy(&y[1], c->sGraph[j].data(), meta::expander_metadata::TIME_MESH_SIZE);

                                x[0] = x[1];
                                y[0] = 0.0f;

                                x += meta::expander_metadata::TIME_MESH_SIZE + 1;
                                y += meta::expander_metadata::TIME_MESH_SIZE + 1;
                                x[0] = x[-1];
                                y[0] = 0.0f;

                                mesh->data(2, meta::expander_metadata::TIME_MESH_SIZE + 2);
                            }
                            else if (j == G_GAIN)
                            {
                                float *x = mesh->pvData[0];
                                float *y = mesh->pvData[1];

                                dsp::copy(&x[2], vTime, meta::expander_metadata::TIME_MESH_SIZE);
                                dsp::copy(&y[2], c->sGraph[j].data(), meta::expander_metadata::TIME_MESH_SIZE);

                                x[0] = x[2] + 0.5f;
                                x[1] = x[0];
                                y[0] = 1.0f;
                                y[1] = y[2];

                                x += meta::expander_metadata::TIME_MESH_SIZE + 2;
                                y += meta::expander_metadata::TIME_MESH_SIZE + 2;
                                x[0] = x[-1] - 0.5f;
                                y[0] = y[-1];
                                x[1] = x[0];
                                y[1] = 1.0f;

                                mesh->data(2, meta::expander_metadata::TIME_MESH_SIZE + 4);
                            }
                            else
                            {
                                dsp::copy(mesh->pvData[0], vTime, meta::expander_metadata::TIME_MESH_SIZE);
                                dsp::copy(mesh->pvData[1], c->sGraph[j].data(), meta::expander_metadata::TIME_MESH_SIZE);
                                mesh->data(2, meta::expander_metadata::TIME_MESH_SIZE);
                            }
                        }
                    } // for j
                }

                bUISync         = false;
            }

            // Output expander curves for each channel
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c       = &vChannels[i];

                // Output expansion curve
                if (c->pCurve != NULL)
                {
                    plug::mesh_t *mesh            = c->pCurve->buffer<plug::mesh_t>();
                    if ((c->nSync & S_CURVE) && (mesh != NULL) && (mesh->isEmpty()))
                    {
                        // Copy frequency points
                        dsp::copy(mesh->pvData[0], vCurve, meta::expander_metadata::CURVE_MESH_SIZE);
                        c->sExp.curve(mesh->pvData[1], vCurve, meta::expander_metadata::CURVE_MESH_SIZE);
                        if (c->fMakeup != 1.0f)
                            dsp::mul_k2(mesh->pvData[1], c->fMakeup, meta::expander_metadata::CURVE_MESH_SIZE);

                        // Mark mesh containing data
                        mesh->data(2, meta::expander_metadata::CURVE_MESH_SIZE);
                        c->nSync &= ~S_CURVE;
                    }
                }

                // Update meter
                if ((c->pMeter[M_ENV] != NULL) && (c->pMeter[M_CURVE] != NULL))
                {
                    c->fDotIn   = c->pMeter[M_ENV]->value();
                    c->fDotOut  = c->sExp.curve(c->fDotIn) * c->fMakeup;
                    c->pMeter[M_CURVE]->set_value(c->fDotOut);
                }
            }

            // Request for redraw
            if (pWrapper != NULL)
                pWrapper->query_display_draw();
        }

        bool expander::inline_display(plug::ICanvas *cv, size_t width, size_t height)
        {
            // Check proportions
            if (height > width)
                height  = width;

            // Init canvas
            if (!cv->init(width, height))
                return false;
            width   = cv->width();
            height  = cv->height();

            // Clear background
            bool bypassing = vChannels[0].sBypass.bypassing();
            cv->set_color_rgb((bypassing) ? CV_DISABLED : CV_BACKGROUND);
            cv->paint();

            float zx    = 1.0f/GAIN_AMP_M_72_DB;
            float zy    = 1.0f/GAIN_AMP_M_72_DB;
            float dx    = width/(logf(GAIN_AMP_P_24_DB)-logf(GAIN_AMP_M_72_DB));
            float dy    = height/(logf(GAIN_AMP_M_72_DB)-logf(GAIN_AMP_P_24_DB));

            // Draw horizontal and vertical lines
            cv->set_line_width(1.0);
            cv->set_color_rgb((bypassing) ? CV_SILVER: CV_YELLOW, 0.5f);
            for (float i=GAIN_AMP_M_72_DB; i<GAIN_AMP_P_24_DB; i *= GAIN_AMP_P_24_DB)
            {
                float ax = dx*(logf(i*zx));
                float ay = height + dy*(logf(i*zy));
                cv->line(ax, 0, ax, height);
                cv->line(0, ay, width, ay);
            }

            // Draw 1:1 line
            cv->set_line_width(2.0);
            cv->set_color_rgb(CV_GRAY);
            {
                float ax1 = dx*(logf(GAIN_AMP_M_72_DB*zx));
                float ax2 = dx*(logf(GAIN_AMP_P_24_DB*zx));
                float ay1 = height + dy*(logf(GAIN_AMP_M_72_DB*zy));
                float ay2 = height + dy*(logf(GAIN_AMP_P_24_DB*zy));
                cv->line(ax1, ay1, ax2, ay2);
            }

            // Draw axis
            cv->set_color_rgb((bypassing) ? CV_SILVER : CV_WHITE);
            {
                float ax = dx*(logf(GAIN_AMP_0_DB*zx));
                float ay = height + dy*(logf(GAIN_AMP_0_DB*zy));
                cv->line(ax, 0, ax, height);
                cv->line(0, ay, width, ay);
            }

            // Reuse display
            pIDisplay           = core::IDBuffer::reuse(pIDisplay, 4, width);
            core::IDBuffer *b   = pIDisplay;
            if (b == NULL)
                return false;

            static const uint32_t c_colors[] =
            {
                CV_MIDDLE_CHANNEL,
                CV_LEFT_CHANNEL, CV_RIGHT_CHANNEL,
                CV_MIDDLE_CHANNEL, CV_SIDE_CHANNEL
            };
            size_t curves       = ((nMode == EM_MONO) || (nMode == EM_STEREO)) ? 1 : 2;
            const uint32_t *vc  = (curves == 1) ? &c_colors[0] :
                                  (nMode == EM_MS) ? &c_colors[3] :
                                  &c_colors[1];

            bool aa = cv->set_anti_aliasing(true);
            lsp_finally { cv->set_anti_aliasing(aa); };
            cv->set_line_width(2);

            for (size_t i=0; i<curves; ++i)
            {
                channel_t *c    = &vChannels[i];

                for (size_t j=0; j<width; ++j)
                {
                    size_t k        = (j*meta::expander_metadata::CURVE_MESH_SIZE)/width;
                    b->v[0][j]      = vCurve[k];
                }
                c->sExp.curve(b->v[1], b->v[0], width);
                if (c->fMakeup != 1.0f)
                    dsp::mul_k2(b->v[1], c->fMakeup, width);

                dsp::fill(b->v[2], 0.0f, width);
                dsp::fill(b->v[3], height, width);
                dsp::axis_apply_log1(b->v[2], b->v[0], zx, dx, width);
                dsp::axis_apply_log1(b->v[3], b->v[1], zy, dy, width);

                // Draw mesh
                uint32_t color = (bypassing || !(active())) ? CV_SILVER : vc[i];
                cv->set_color_rgb(color);
                cv->draw_lines(b->v[2], b->v[3], width);
            }

            // Draw dot
            if (active())
            {
                size_t channels     = ((nMode == EM_MONO) || ((nMode == EM_STEREO) && (!bStereoSplit))) ? 1 : 2;
                const uint32_t *vd  = (channels == 1) ? &c_colors[0] :
                                      (nMode == EM_MS) ? &c_colors[3] :
                                      &c_colors[1];

                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c    = &vChannels[i];

                    uint32_t color = (bypassing) ? CV_SILVER : vd[i];
                    Color c1(color), c2(color);
                    c2.alpha(0.9);

                    float ax = dx*(logf(c->fDotIn*zx));
                    float ay = height + dy*(logf(c->fDotOut*zy));

                    cv->radial_gradient(ax, ay, c1, c2, 12);
                    cv->set_color_rgb(0);
                    cv->circle(ax, ay, 4);
                    cv->set_color_rgb(color);
                    cv->circle(ax, ay, 3);
                }
            }

            return true;
        }

        void expander::dump(dspu::IStateDumper *v) const
        {
            plug::Module::dump(v);

            size_t channels = (nMode == EM_MONO) ? 1 : 2;

            v->write("nMode", nMode);
            v->write("nChannels", channels);
            v->write("bSidechain", bSidechain);

            v->begin_array("vChannels", vChannels, channels);
            for (size_t i=0; i<channels; ++i)
            {
                const channel_t *c = &vChannels[i];

                v->begin_object(c, sizeof(channel_t));
                {
                    v->write_object("sBypass", &c->sBypass);
                    v->write_object("sSC", &c->sSC);
                    v->write_object("sSCEq", &c->sSCEq);
                    v->write_object("sExp", &c->sExp);
                    v->write_object("sLaDelay", &c->sLaDelay);
                    v->write_object("sInDelay", &c->sInDelay);
                    v->write_object("sOutDelay", &c->sInDelay);
                    v->write_object("sDryDelay", &c->sDryDelay);

                    v->begin_array("sGraph", c->sGraph, G_TOTAL);
                    for (size_t j=0; j<G_TOTAL; ++j)
                        v->write_object(&c->sGraph[j]);
                    v->end_array();

                    v->write("vIn", c->vIn);
                    v->write("vOut", c->vOut);
                    v->write("vSc", c->vSc);
                    v->write("vEnv", c->vEnv);
                    v->write("vGain", c->vGain);
                    v->write("bScListen", c->bScListen);
                    v->write("nSync", c->nSync);
                    v->write("nScType", c->nScType);
                    v->write("fMakeup", c->fMakeup);
                    v->write("fDryGain", c->fDryGain);
                    v->write("fWetGain", c->fWetGain);
                    v->write("fDotIn", c->fDotIn);
                    v->write("fDotOut", c->fDotOut);

                    v->write("pIn", c->pIn);
                    v->write("pOut", c->pOut);
                    v->write("pSC", c->pSC);
                    v->write("pShmIn", c->pShmIn);
                    v->begin_array("pGraph", c->pGraph, G_TOTAL);
                    for (size_t j=0; j<G_TOTAL; ++j)
                        v->write(c->pGraph[j]);
                    v->end_array();
                    v->begin_array("pMeter", c->pGraph, M_TOTAL);
                    for (size_t j=0; j<M_TOTAL; ++j)
                        v->write(c->pMeter[j]);
                    v->end_array();

                    v->write("pScType", c->pScType);
                    v->write("pScMode", c->pScMode);
                    v->write("pScLookahead", c->pScLookahead);
                    v->write("pScListen", c->pScListen);
                    v->write("pScSource", c->pScSource);
                    v->write("pScReactivity", c->pScReactivity);
                    v->write("pScPreamp", c->pScPreamp);
                    v->write("pScHpfMode", c->pScHpfMode);
                    v->write("pScHpfFreq", c->pScHpfFreq);
                    v->write("pScLpfMode", c->pScLpfMode);
                    v->write("pScLpfFreq", c->pScLpfFreq);

                    v->write("pMode", c->pMode);
                    v->write("pAttackLvl", c->pAttackLvl);
                    v->write("pReleaseLvl", c->pReleaseLvl);
                    v->write("pAttackTime", c->pAttackTime);
                    v->write("pReleaseTime", c->pReleaseTime);
                    v->write("pHoldTime", c->pHoldTime);
                    v->write("pRatio", c->pRatio);
                    v->write("pKnee", c->pKnee);
                    v->write("pMakeup", c->pMakeup);

                    v->write("pDryGain", c->pDryGain);
                    v->write("pWetGain", c->pWetGain);
                    v->write("pDryWet", c->pDryWet);
                    v->write("pCurve", c->pCurve);
                    v->write("pReleaseOut", c->pReleaseOut);
                }
                v->end_object();
            }
            v->end_array();

            v->write("vCurve", vCurve);
            v->write("vTime", vTime);
            v->write("bPause", bPause);
            v->write("bClear", bClear);
            v->write("bMSListen", bMSListen);
            v->write("bStereoSplit", bStereoSplit);
            v->write("fInGain", fInGain);
            v->write("bUISync", bUISync);
            v->write("pIDisplay", pIDisplay);

            v->write("pBypass", pBypass);
            v->write("pInGain", pInGain);
            v->write("pOutGain", pOutGain);
            v->write("pPause", pPause);
            v->write("pClear", pClear);
            v->write("pMSListen", pMSListen);
            v->write("pStereoSplit", pStereoSplit);
            v->write("pScSpSource", pScSpSource);

            v->write("pData", pData);
        }
    } /* namespace plugins */
} /* namespace lsp */


