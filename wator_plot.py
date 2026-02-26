"""
Wa-Tor — Visualizador de Población vs Tiempo
Uso: python wator_plot.py [archivo.csv]
"""

import sys
import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy.signal import find_peaks

# ─── Cargar datos ─────────────────────────────────────────────────────────────
csv_path = sys.argv[1] if len(sys.argv) > 1 else "wator_poblacion.csv"

if not os.path.exists(csv_path):
    print(f"[ERROR] No se encontró '{csv_path}'")
    sys.exit(1)

df        = pd.read_csv(csv_path)
t         = df["t"].values
peces     = df["peces"].values
tiburones = df["tiburones"].values

# ─── Colores y estilo ─────────────────────────────────────────────────────────
BG      = "#0a0f1e"
PANEL   = "#111827"
GRID    = "#1e2a3a"
TEXT    = "#e2e8f0"
FISH    = "#00d4ff"
SHARK   = "#ff4444"
ACCENT  = "#f59e0b"

plt.rcParams.update({
    "font.family":      "monospace",
    "text.color":       TEXT,
    "axes.labelcolor":  TEXT,
    "xtick.color":      TEXT,
    "ytick.color":      TEXT,
    "figure.facecolor": BG,
    "axes.facecolor":   PANEL,
    "axes.edgecolor":   GRID,
    "axes.grid":        True,
    "grid.color":       GRID,
    "grid.linewidth":   0.7,
    "grid.alpha":       1.0,
})

# ─── Figura ───────────────────────────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(14, 6), facecolor=BG)
fig.suptitle("🌊  WA-TOR  —  Población vs Tiempo",
             fontsize=14, fontweight="bold", color=TEXT, y=1.01)

# Áreas sombreadas
ax.fill_between(t, peces,     alpha=0.12, color=FISH)
ax.fill_between(t, tiburones, alpha=0.12, color=SHARK)

# Líneas principales
ax.plot(t, peces,     color=FISH,  lw=2.0, label="Peces 🐟",     zorder=3)
ax.plot(t, tiburones, color=SHARK, lw=2.0, label="Tiburones 🦈", zorder=3)

# Picos
picos_p, _ = find_peaks(peces,     distance=5)
picos_t, _ = find_peaks(tiburones, distance=5)

if len(picos_p):
    ax.scatter(t[picos_p], peces[picos_p],
               color=FISH,  s=30, zorder=5, alpha=0.75, linewidths=0)
if len(picos_t):
    ax.scatter(t[picos_t], tiburones[picos_t],
               color=SHARK, s=30, zorder=5, alpha=0.75, linewidths=0)

# Líneas de media
ax.axhline(peces.mean(),     color=FISH,  lw=1, linestyle="--",
           alpha=0.5, label=f"Media peces: {peces.mean():.0f}")
ax.axhline(tiburones.mean(), color=SHARK, lw=1, linestyle="--",
           alpha=0.5, label=f"Media tiburones: {tiburones.mean():.0f}")

# Ejes y leyenda
ax.set_xlabel("Paso de tiempo (t)", fontsize=11)
ax.set_ylabel("Número de individuos", fontsize=11)
ax.set_xlim(t[0], t[-1])
ax.set_ylim(bottom=0)

# Anotación de desfase si hay suficientes picos
if len(picos_p) > 0 and len(picos_t) > 0:
    # Calcular desfase promedio entre pico de peces y siguiente pico de tiburones
    desfases = []
    for pp in picos_p:
        siguientes = picos_t[picos_t > pp]
        if len(siguientes):
            desfases.append(t[siguientes[0]] - t[pp])
    if desfases:
        d = np.mean(desfases)
        ax.annotate(f"Desfase medio: {d:.1f} pasos",
                    xy=(0.98, 0.95), xycoords="axes fraction",
                    ha="right", va="top", fontsize=9,
                    color=ACCENT,
                    bbox=dict(boxstyle="round,pad=0.3", fc=PANEL, ec=GRID, lw=1))

leg = ax.legend(loc="upper left", facecolor=PANEL, edgecolor=GRID,
                labelcolor=TEXT, fontsize=10, framealpha=1)

plt.tight_layout()

# ─── Guardar y mostrar ────────────────────────────────────────────────────────
out = "wator_resultado.png"
plt.savefig(out, dpi=150, bbox_inches="tight", facecolor=BG)
print(f"  ✅ Guardado en: {out}")
plt.show()
