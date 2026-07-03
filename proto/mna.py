"""Minimal MNA (modified nodal analysis) for linear R/C networks driven by
one voltage source, discretized with the bilinear (trapezoidal) transform.

This is the linear core of the nodal DK-method: build G and C matrices from
a netlist, then per sample solve
    (G + 2*fs*C) v[n] = 2*fs*C v[n-1] + C dv[n-1] + b*u  ->  precomputed as
    v[n] = M v[n-1] + K (u[n] + u[n-1]) - M2 ...
implemented in the standard companion form:
    A1 x[n] = A2 x[n-1] + B (u[n] + u[n-1])
where x = node voltages (+ source current), A1 = G + 2fs*C, A2 = -G + 2fs*C
with the source row handled as (u[n]) not averaged — see below.

Rebuild (cheap, tiny matrices) whenever a pot moves.
"""

import numpy as np


class LinearNetwork:
    def __init__(self, n_nodes, fs):
        """n_nodes internal nodes (1..n), node 0 = ground.
        One ideal voltage source drives node `src_node`."""
        self.n = n_nodes
        self.fs = fs
        self.G = np.zeros((n_nodes, n_nodes))
        self.C = np.zeros((n_nodes, n_nodes))
        self.src_node = None

    def _stamp(self, M, a, b, val):
        if a > 0:
            M[a - 1, a - 1] += val
        if b > 0:
            M[b - 1, b - 1] += val
        if a > 0 and b > 0:
            M[a - 1, b - 1] -= val
            M[b - 1, a - 1] -= val

    def R(self, a, b, ohms):
        self._stamp(self.G, a, b, 1.0 / ohms)
        return self

    def Cap(self, a, b, farads):
        self._stamp(self.C, a, b, farads)
        return self

    def Vsrc(self, node):
        self.src_node = node
        return self

    def compile(self, out_node):
        """MNA with source row: unknowns x = [v1..vn, i_src]."""
        n = self.n
        A1 = np.zeros((n + 1, n + 1))
        A2 = np.zeros((n + 1, n + 1))
        k = 2.0 * self.fs
        A1[:n, :n] = self.G + k * self.C
        A2[:n, :n] = -self.G + k * self.C
        s = self.src_node - 1
        A1[s, n] = 1.0
        A1[n, s] = 1.0
        A2[n, s] = -1.0          # v_s[n] + v_s[n-1] = u[n] + u[n-1]
        B = np.zeros(n + 1)
        B[n] = 1.0
        self._M = np.linalg.solve(A1, A2)
        self._K = np.linalg.solve(A1, B)
        self._x = np.zeros(n + 1)
        self._u1 = 0.0
        self._out = out_node - 1
        return self

    def process(self, u):
        y = np.empty_like(u)
        x, M, K = self._x, self._M, self._K
        u1 = self._u1
        out = self._out
        for i in range(len(u)):
            x = M @ x + K * (u[i] + u1)
            u1 = u[i]
            y[i] = x[out]
        self._x, self._u1 = x, u1
        return y

    def freq_response(self, freqs, out_node):
        """Analytic continuous-time response for validation."""
        n = self.n
        s_idx = self.src_node - 1
        H = np.empty(len(freqs), dtype=complex)
        for i, f in enumerate(freqs):
            s = 2j * np.pi * f
            A = np.zeros((n + 1, n + 1), dtype=complex)
            A[:n, :n] = self.G + s * self.C
            A[s_idx, n] = 1.0
            A[n, s_idx] = 1.0
            b = np.zeros(n + 1, dtype=complex)
            b[n] = 1.0
            x = np.linalg.solve(A, b)
            H[i] = x[out_node - 1]
        return H


def fender_tone_stack(fs, treble=0.5, bass=0.5, volume=0.7, Rsrc=38e3):
    """AA1164 FMV tone stack + volume pot, driven from V1A plate through its
    Thevenin source impedance Rsrc ≈ ra||Rp.

    Nodes: 1=ideal source, 2=plate node, 3=treble-cap top, 4=slope node,
           5=bass-mid node, 6=treble wiper (vol top), 7=vol wiper (out),
           8=bass-cap bottom.
    Audio (log) taper approximated as p^2.
    """
    RT, RB, RV = 250e3, 250e3, 1e6
    tw = max(min(treble, 0.999), 1e-3) ** 2
    bw = max(min(bass, 0.999), 1e-3) ** 2
    vw = max(min(volume, 0.999), 1e-3) ** 2

    net = LinearNetwork(8, fs)
    net.Vsrc(1)
    net.R(1, 2, max(Rsrc, 1.0))          # V1A plate output impedance
    net.Cap(2, 3, 250e-12)               # treble cap
    net.R(3, 6, max((1.0 - tw) * RT, 1.0))   # treble pot upper
    net.R(6, 5, max(tw * RT, 1.0))           # treble pot lower
    net.R(2, 4, 100e3)                   # slope resistor
    net.Cap(4, 8, 0.1e-6)                # bass cap
    net.R(8, 5, max(bw * RB, 1.0))       # bass pot (rheostat)
    net.Cap(4, 5, 0.047e-6)              # mid cap
    net.R(5, 0, 6800.0)                  # fixed mid resistor
    net.R(6, 7, max((1.0 - vw) * RV, 1.0))   # volume pot upper
    net.R(7, 0, max(vw * RV, 1.0))           # volume pot lower
    return net
