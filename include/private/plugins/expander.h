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

#ifndef PRIVATE_PLUGINS_EXPANDER_H_
#define PRIVATE_PLUGINS_EXPANDER_H_

#include <lsp-plug.in/plug-fw/plug.h>
#include <lsp-plug.in/plug-fw/core/IDBuffer.h>
#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/dsp-units/dynamics/Expander.h>
#include <lsp-plug.in/dsp-units/filters/Equalizer.h>
#include <lsp-plug.in/dsp-units/util/Delay.h>
#include <lsp-plug.in/dsp-units/util/MeterGraph.h>
#include <lsp-plug.in/dsp-units/util/Sidechain.h>

#include <private/meta/expander.h>

namespace lsp
{
    namespace plugins
    {
        /**
         * Expander Plugin Series
         */
        class expander: public plug::Module
        {
            public:
                enum exp_mode_t
                {
                    EM_MONO,
                    EM_STEREO,
                    EM_LR,
                    EM_MS
                };

            protected:
                enum sc_source_t
                {
                    SCT_INTERNAL,
                    SCT_EXTERNAL
                };

                enum sc_graph_t
                {
                    G_IN,
                    G_SC,
                    G_ENV,
                    G_GAIN,
                    G_OUT,

                    G_TOTAL
                };

                enum sc_meter_t
                {
                    M_IN,
                    M_SC,
                    M_ENV,
                    M_GAIN,
                    M_CURVE,
                    M_OUT,

                    M_TOTAL
                };

                enum sync_t
                {
                    S_CURVE     = 1 << 0,

                    S_ALL       = S_CURVE
                };

                typedef struct channel_t
                {
                    dspu::Bypass            sBypass;            // Bypass
                    dspu::Sidechain         sSC;                // Sidechain module
                    dspu::Equalizer         sSCEq;              // Sidechain equalizer
                    dspu::Expander          sExp;               // Expansion module
                    dspu::Delay             sLaDelay;           // Lookahead delay
                    dspu::Delay             sInDelay;           // Input compensation delay
                    dspu::Delay             sOutDelay;          // Output compensation delay
                    dspu::Delay             sDryDelay;          // Dry delay
                    dspu::MeterGraph        sGraph[G_TOTAL];    // Input meter graph

                    float                  *vIn;                // Input data
                    float                  *vOut;               // Output data
                    float                  *vSc;                // Sidechain data
                    float                  *vEnv;               // Envelope data
                    float                  *vGain;              // Gain reduction data
                    bool                    bScListen;          // Listen sidechain
                    size_t                  nSync;              // Synchronization flags
                    size_t                  nScType;            // Sidechain mode
                    float                   fMakeup;            // Makeup gain
                    float                   fDryGain;           // Dry gain
                    float                   fWetGain;           // Wet gain
                    float                   fDotIn;             // Dot input gain
                    float                   fDotOut;            // Dot output gain

                    plug::IPort            *pIn;                // Input port
                    plug::IPort            *pOut;               // Output port
                    plug::IPort            *pSC;                // Sidechain port

                    plug::IPort            *pGraph[G_TOTAL];    // History graphs
                    plug::IPort            *pMeter[M_TOTAL];    // Meters

                    plug::IPort            *pScType;            // Sidechain location
                    plug::IPort            *pScMode;            // Sidechain mode
                    plug::IPort            *pScLookahead;       // Sidechain lookahead
                    plug::IPort            *pScListen;          // Sidechain listen
                    plug::IPort            *pScSource;          // Sidechain source
                    plug::IPort            *pScReactivity;      // Sidechain reactivity
                    plug::IPort            *pScPreamp;          // Sidechain pre-amplification
                    plug::IPort            *pScHpfMode;         // Sidechain high-pass filter mode
                    plug::IPort            *pScHpfFreq;         // Sidechain high-pass filter frequency
                    plug::IPort            *pScLpfMode;         // Sidechain low-pass filter mode
                    plug::IPort            *pScLpfFreq;         // Sidechain low-pass filter frequency

                    plug::IPort            *pMode;              // Mode
                    plug::IPort            *pAttackLvl;         // Attack level
                    plug::IPort            *pReleaseLvl;        // Release level
                    plug::IPort            *pAttackTime;        // Attack time
                    plug::IPort            *pReleaseTime;       // Release time
                    plug::IPort            *pRatio;             // Ratio
                    plug::IPort            *pKnee;              // Knee
                    plug::IPort            *pMakeup;            // Makeup

                    plug::IPort            *pDryGain;           // Dry gain
                    plug::IPort            *pWetGain;           // Wet gain
                    plug::IPort            *pCurve;             // Curve graph
                    plug::IPort            *pReleaseOut;        // Output release level
                } channel_t;

            protected:
                size_t                  nMode;          // Expander mode
                bool                    bSidechain;     // External side chain
                channel_t              *vChannels;      // Expander channels
                float                  *vCurve;         // Expander curve
                float                  *vTime;          // Time points buffer
                bool                    bPause;         // Pause button
                bool                    bClear;         // Clear button
                bool                    bMSListen;      // Mid/Side listen
                bool                    bStereoSplit;   // Stereo split
                float                   fInGain;        // Input gain
                bool                    bUISync;        // UI Sync
                core::IDBuffer         *pIDisplay;      // Inline display buffer

                plug::IPort            *pBypass;        // Bypass port
                plug::IPort            *pInGain;        // Input gain
                plug::IPort            *pOutGain;       // Output gain
                plug::IPort            *pPause;         // Pause gain
                plug::IPort            *pClear;         // Cleanup gain
                plug::IPort            *pMSListen;      // Mid/Side listen
                plug::IPort            *pStereoSplit;   // Stereo split mode
                plug::IPort            *pScSpSource;    // Sidechain source for stereo split mode

                uint8_t                *pData;          // Expander data

            protected:
                static dspu::sidechain_source_t     decode_sidechain_source(int source, bool split, size_t channel);

            public:
                expander(const meta::plugin_t *metadata, bool sc, size_t mode);
                virtual ~expander() override;

                virtual void        init(plug::IWrapper *wrapper, plug::IPort **ports) override;
                virtual void        destroy() override;

            public:
                virtual void        update_settings() override;
                virtual void        update_sample_rate(long sr) override;
                virtual void        ui_activated() override;

                virtual void        process(size_t samples) override;
                virtual bool        inline_display(plug::ICanvas *cv, size_t width, size_t height) override;

                virtual void        dump(dspu::IStateDumper *v) const override;
        };
    } /* namespace plugins */
} /* namespace lsp */

#endif /* PRIVATE_PLUGINS_EXPANDER_H_ */
