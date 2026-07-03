"""Vacuum tube compact models — Norman Koren improved equations.

Triode:
    E1 = (Vpk/KP) * log(1 + exp(KP*(1/MU + Vgk/sqrt(KVB + Vpk^2))))
    Ip = 2 * E1^EX / KG1          (E1 > 0, else 0)

Beam tetrode / pentode:
    E1 = (Vg2k/KP) * log(1 + exp(KP*(1/MU + Vg1k/Vg2k)))
    Ip = (E1^EX / KG1) * atan(Vpk / KVB)
    Ig2 = E1^EX / KG2

Parameter sets are the widely used Koren/Duncan fits; marked to verify
against measured curves in M0-amp.
"""

from dataclasses import dataclass
import numpy as np


@dataclass
class TriodeParams:
    name: str
    MU: float
    EX: float
    KG1: float
    KP: float
    KVB: float

    def ip(self, vgk, vpk):
        vpk = max(vpk, 0.0)
        e1 = (vpk / self.KP) * np.log1p(
            np.exp(np.clip(self.KP * (1.0 / self.MU
                                      + vgk / np.sqrt(self.KVB + vpk * vpk)),
                           -50.0, 50.0)))
        if e1 <= 0.0:
            return 0.0
        return 2.0 * e1 ** self.EX / self.KG1


@dataclass
class PentodeParams:
    name: str
    MU: float
    EX: float
    KG1: float
    KG2: float
    KP: float
    KVB: float

    def currents(self, vg1k, vg2k, vpk):
        """Returns (Ip, Ig2)."""
        vpk = max(vpk, 0.0)
        vg2k = max(vg2k, 1.0)
        e1 = (vg2k / self.KP) * np.log1p(
            np.exp(np.clip(self.KP * (1.0 / self.MU + vg1k / vg2k),
                           -50.0, 50.0)))
        if e1 <= 0.0:
            return 0.0, 0.0
        core = e1 ** self.EX
        ip = (core / self.KG1) * np.arctan(vpk / self.KVB)
        ig2 = core / self.KG2
        return ip, ig2


# standard Koren parameter sets (to verify against datasheet curves)
T_12AX7 = TriodeParams("12AX7", MU=100.0, EX=1.4, KG1=1060.0, KP=600.0,
                       KVB=300.0)
T_12AT7 = TriodeParams("12AT7", MU=60.0, EX=1.35, KG1=460.0, KP=300.0,
                       KVB=300.0)
# KG1 calibrated to the datasheet point Vp=Vg2=250V, Vg1=-12.5V -> Ip=45mA
# (the widely circulated KG1=1672 lands at 15.7mA there, off by ~3x)
P_6V6 = PentodeParams("6V6GT", MU=12.7, EX=1.31, KG1=583.0, KG2=4500.0,
                      KP=41.5, KVB=12.7)


def datasheet_sanity():
    """Datasheet typical operating points the models must land near."""
    checks = []
    ip = T_12AX7.ip(-2.0, 250.0) * 1e3
    checks.append(("12AX7  Vp=250 Vg=-2   -> Ip", ip, 1.2, 0.35))
    ip = T_12AT7.ip(-2.0, 250.0) * 1e3
    checks.append(("12AT7  Vp=250 Vg=-2   -> Ip", ip, 10.0, 0.45))
    ip, ig2 = P_6V6.currents(-12.5, 250.0, 250.0)
    checks.append(("6V6    Vp=Vg2=250 Vg=-12.5 -> Ip", ip * 1e3, 45.0, 0.35))
    checks.append(("6V6    (same)          -> Ig2", ig2 * 1e3, 4.5, 0.6))
    return checks
