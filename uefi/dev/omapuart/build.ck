/*++

Copyright (c) 2014 Minoca Corp.

    This file is licensed under the terms of the GNU General Public License
    version 3. Alternative licensing terms are available. Contact
    info@minocacorp.com for details. See the LICENSE file at the root of this
    project for complete licensing information.

Module Name:

    OMAP UART

Abstract:

    This library contains the UART controller library used in Texas
    Instruments OMAP3 and OMAP4 SoCs.

Author:

    Evan Green 27-Feb-2014

Environment:

    Firmware

--*/

function build() {
    sources = [
        "omapuart.c"
    ];

    includes = [
        "$//uefi/include"
    ];

    sources_config = {
        "CFLAGS": ["-fshort-wchar"],
    };

    lib = {
        "label": "omapuart",
        "inputs": sources,
        "sources_config": sources_config,
        "includes": includes
    };

    entries = static_library(lib);
    return entries;
}

return build();
