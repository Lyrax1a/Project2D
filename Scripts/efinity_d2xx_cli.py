"""Run Efinity's FTDI CLI through its bundled D2XX backend on Windows.

Efinity 2025.2 ships both PyUSB and D2XX providers.  FT4232 interfaces using
the normal FTDI VCP driver cannot be opened by PyUSB, so let discovery fall
through to the vendor D2XX provider instead of replacing a Windows driver.
"""

from __future__ import annotations

import os
import runpy
import sys
from pathlib import Path

import usb.core

from efx_pgm import usb_resolver
from efx_pgm.hiftdi import jtag as hiftdi_jtag


_original_find_devices = usb_resolver.PyFTDIDeviceResolver.find_devices


def _safe_find_devices(self, vendor: int, product: int):
    try:
        return _original_find_devices(self, vendor, product)
    except usb.core.USBError:
        return []


usb_resolver.PyFTDIDeviceResolver.find_devices = _safe_find_devices

# Efinity 2025.2's caller supplies an optional initialization argument that
# the HiFTDI implementation bundled in the same release does not accept.
_original_jtag_configure = hiftdi_jtag.JtagEngine.configure


def _compatible_jtag_configure(self, url: str, _init=None):
    return _original_jtag_configure(self, url)


hiftdi_jtag.JtagEngine.configure = _compatible_jtag_configure
programmer = Path(os.environ["EFXPGM_HOME"]) / "bin" / "efx_pgm" / "ftdi_program.py"
sys.argv[0] = str(programmer)
runpy.run_path(str(programmer), run_name="__main__")
