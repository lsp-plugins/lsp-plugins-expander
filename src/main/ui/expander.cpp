/*
 * Copyright (C) 2021 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2021 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#include <private/plugins/expander.h>
#include <lsp-plug.in/plug-fw/ui.h>

namespace lsp
{
    namespace plugui
    {
        //---------------------------------------------------------------------
        // Plugin UI factory
        static const meta::plugin_t *uis[] =
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

        static ui::Factory factory(uis, 8);

    } // namespace plugui
} // namespace lsp


