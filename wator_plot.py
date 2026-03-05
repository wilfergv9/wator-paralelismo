"""
Wa-Tor — Visualizador de Población + Rendimiento OpenMP
Uso: python wator_plot.py [archivo.csv]
     python wator_plot.py run1.csv run2.csv run3.csv   ← comparar varias ejecuciones
"""

import sys
import os
import re
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.ticker import MaxNLocator
from scipy.signal import find_peaks

# ─── Colores ──────────────────────────────────────────────────────────────────
BG      = "#0a0f1e"
PANEL   = "#111827"
GRID    = "#1e2a3a"
TEXT    = "#e2e8f0"
FISH    = "#00d4ff"
SHARK   = "#ff4444"
ACCENT  = "#f59e0b"
GREEN   = "#22c55e"
PURPLE  = "#a78bfa"

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

# ─── Leer metadatos del CSV (#-comments) ─────────────────────────────────────
def leer_meta(path):
    meta = {"n_hilos": "?", "grilla": "?"}
    with open(path) as f:
        for line in f:
            if not line.startswith("#"):
                break
            m = re.match(r"#\s*(\w+)=(.*)", line.strip())
            if m:
                meta[m.group(1)] = m.group(2).strip()
    return meta

# ─── Cargar archivo(s) ────────────────────────────────────────────────────────
paths = sys.argv[1:] if len(sys.argv) > 1 else ["wator_poblacion.csv"]
for p in paths:
    if not os.path.exists(p):
        print(f"[ERROR] No se encontró '{p}'")
        sys.exit(1)

datasets = []
for p in paths:
    meta = leer_meta(p)
    df   = pd.read_csv(p, comment="#")
    datasets.append({"path": p, "df": df, "meta": meta,
                     "label": f"{os.path.basename(p)} ({meta['n_hilos']} hilos)"})

# Usar el primer dataset como principal
main      = datasets[0]
df        = main["df"]
t         = df["t"].values
peces     = df["peces"].values
tiburones = df["tiburones"].values
tiene_ms  = "tiempo_ms" in df.columns
ms_paso   = df["tiempo_ms"].values if tiene_ms else None
n_hilos   = main["meta"]["n_hilos"]
grilla    = main["meta"]["grilla"]
T         = len(t)

# ─── Layout ───────────────────────────────────────────────────────────────────
# Si hay varios CSVs (comparación de speedup) → 3 filas
# Si hay un solo CSV                          → 2 filas
multi = len(datasets) > 1
nrows = 3 if multi else 2

fig = plt.figure(figsize=(16, 5 * nrows), facecolor=BG)
fig.suptitle(
    f"🌊  WA-TOR  —  Población & Rendimiento OpenMP   "
    f"[{grilla}  ·  {n_hilos} hilos]",
    fontsize=14, fontweight="bold", color=TEXT, y=0.99
)

gs = gridspec.GridSpec(
    nrows, 2, figure=fig,
    hspace=0.50, wspace=0.35,
    left=0.07, right=0.97,
    top=0.95, bottom=0.05
)

# ══════════════════════════════════════════════════════════════════════════════
# FILA 0 — Población vs Tiempo (ancho completo)
# ══════════════════════════════════════════════════════════════════════════════
ax_pop = fig.add_subplot(gs[0, :])
ax_pop.set_title("Población vs Tiempo", color=ACCENT, fontsize=11, pad=8)

ax_pop.fill_between(t, peces,     alpha=0.12, color=FISH)
ax_pop.fill_between(t, tiburones, alpha=0.12, color=SHARK)
ax_pop.plot(t, peces,     color=FISH,  lw=2.0, label="Peces 🐟")
ax_pop.plot(t, tiburones, color=SHARK, lw=2.0, label="Tiburones 🦈")

# Picos
picos_p, _ = find_peaks(peces,     distance=5)
picos_t, _ = find_peaks(tiburones, distance=5)
if len(picos_p):
    ax_pop.scatter(t[picos_p], peces[picos_p],
                   color=FISH,  s=28, zorder=5, alpha=0.8, linewidths=0)
if len(picos_t):
    ax_pop.scatter(t[picos_t], tiburones[picos_t],
                   color=SHARK, s=28, zorder=5, alpha=0.8, linewidths=0)

# Medias
ax_pop.axhline(peces.mean(),     color=FISH,  lw=1.0, ls="--", alpha=0.5,
               label=f"Media peces: {peces.mean():.0f}")
ax_pop.axhline(tiburones.mean(), color=SHARK, lw=1.0, ls="--", alpha=0.5,
               label=f"Media tiburones: {tiburones.mean():.0f}")

# Desfase medio
if len(picos_p) > 0 and len(picos_t) > 0:
    desfases = []
    for pp in picos_p:
        sig = picos_t[picos_t > pp]
        if len(sig):
            desfases.append(t[sig[0]] - t[pp])
    if desfases:
        ax_pop.annotate(
            f"Desfase medio: {np.mean(desfases):.1f} pasos",
            xy=(0.98, 0.95), xycoords="axes fraction",
            ha="right", va="top", fontsize=9, color=ACCENT,
            bbox=dict(boxstyle="round,pad=0.3", fc=PANEL, ec=GRID, lw=1)
        )

ax_pop.set_xlabel("Paso de tiempo (t)")
ax_pop.set_ylabel("Individuos")
ax_pop.set_xlim(t[0], t[-1])
ax_pop.set_ylim(bottom=0)
ax_pop.legend(loc="upper left", facecolor=PANEL, edgecolor=GRID,
              labelcolor=TEXT, fontsize=10, framealpha=1)

# ══════════════════════════════════════════════════════════════════════════════
# FILA 1 — Tiempo por paso  +  Distribución de tiempos
# ══════════════════════════════════════════════════════════════════════════════
if tiene_ms:
    # ── Tiempo por paso (serie temporal) ─────────────────────────────────────
    ax_ms = fig.add_subplot(gs[1, 0])
    ax_ms.set_title("Tiempo por paso (ms)", color=ACCENT, fontsize=11, pad=8)

    ax_ms.fill_between(t, ms_paso, alpha=0.15, color=GREEN)
    ax_ms.plot(t, ms_paso, color=GREEN, lw=1.4, alpha=0.9)

    # Media móvil suavizada (ventana 20)
    if T >= 20:
        kernel  = np.ones(20) / 20
        suavizado = np.convolve(ms_paso, kernel, mode="valid")
        t_suav    = t[19:]
        ax_ms.plot(t_suav, suavizado, color=ACCENT, lw=2.0, label="Media móvil (20)")
        ax_ms.legend(facecolor=PANEL, edgecolor=GRID, labelcolor=TEXT, fontsize=9)

    ax_ms.axhline(ms_paso.mean(), color=GREEN, lw=1.0, ls="--", alpha=0.6)
    ax_ms.annotate(
        f"μ = {ms_paso.mean():.3f} ms\n"
        f"σ = {ms_paso.std():.3f} ms\n"
        f"⚙  {n_hilos} hilos",
        xy=(0.98, 0.95), xycoords="axes fraction",
        ha="right", va="top", fontsize=9, color=GREEN,
        bbox=dict(boxstyle="round,pad=0.4", fc=PANEL, ec=GRID, lw=1)
    )
    ax_ms.set_xlabel("Paso de tiempo (t)")
    ax_ms.set_ylabel("ms / paso")
    ax_ms.set_xlim(t[0], t[-1])
    ax_ms.set_ylim(bottom=0)

    # ── Distribución de tiempos (histograma) ──────────────────────────────────
    ax_hist = fig.add_subplot(gs[1, 1])
    ax_hist.set_title("Distribución de Tiempo por Paso", color=ACCENT,
                      fontsize=11, pad=8)

    ax_hist.hist(ms_paso, bins=40, color=GREEN, alpha=0.7, edgecolor="none")
    ax_hist.axvline(ms_paso.mean(),                  color=ACCENT, lw=2,
                    ls="--", label=f"Media  {ms_paso.mean():.3f} ms")
    ax_hist.axvline(np.percentile(ms_paso, 95),      color="#f87171", lw=1.5,
                    ls=":",  label=f"P95    {np.percentile(ms_paso,95):.3f} ms")
    ax_hist.set_xlabel("ms / paso")
    ax_hist.set_ylabel("Frecuencia")
    ax_hist.legend(facecolor=PANEL, edgecolor=GRID, labelcolor=TEXT, fontsize=9)

    # Throughput: pasos/segundo
    throughput = 1000.0 / ms_paso.mean() if ms_paso.mean() > 0 else 0
    ax_hist.annotate(
        f"≈ {throughput:.1f} pasos/s",
        xy=(0.97, 0.60), xycoords="axes fraction",
        ha="right", va="top", fontsize=10, color=ACCENT, fontweight="bold",
        bbox=dict(boxstyle="round,pad=0.4", fc=PANEL, ec=ACCENT, lw=1.2)
    )

else:
    # Sin columna tiempo_ms → aviso
    ax_info = fig.add_subplot(gs[1, :])
    ax_info.axis("off")
    ax_info.text(0.5, 0.5,
                 "El CSV no contiene la columna 'tiempo_ms'.\n"
                 "Recompila el simulador y vuelve a correrlo.",
                 ha="center", va="center", fontsize=12,
                 color="#f87171", transform=ax_info.transAxes)

# ══════════════════════════════════════════════════════════════════════════════
# FILA 2 (opcional) — Comparación de speedup entre múltiples ejecuciones
# ══════════════════════════════════════════════════════════════════════════════
if multi:
    # Recopilar medias de tiempo por ejecución
    etiquetas, medias, errores, hilos_list = [], [], [], []
    for ds in datasets:
        d = ds["df"]
        if "tiempo_ms" not in d.columns:
            continue
        ms = d["tiempo_ms"].values
        etiquetas.append(ds["meta"].get("n_hilos", "?"))
        medias.append(ms.mean())
        errores.append(ms.std())
        try:
            hilos_list.append(int(ds["meta"].get("n_hilos", 0)))
        except ValueError:
            hilos_list.append(0)

    if medias:
        # ── Barras de tiempo medio ────────────────────────────────────────────
        ax_bar = fig.add_subplot(gs[2, 0])
        ax_bar.set_title("Tiempo medio por paso (ms)", color=ACCENT,
                         fontsize=11, pad=8)

        x     = np.arange(len(etiquetas))
        bars  = ax_bar.bar(x, medias, yerr=errores, color=PURPLE,
                           alpha=0.8, capsize=5, error_kw={"ecolor": TEXT, "lw": 1.5})
        ax_bar.set_xticks(x)
        ax_bar.set_xticklabels([f"{e} hilos" for e in etiquetas])
        ax_bar.set_ylabel("ms / paso")
        ax_bar.set_ylim(bottom=0)
        for bar, val in zip(bars, medias):
            ax_bar.text(bar.get_x() + bar.get_width()/2, bar.get_height() + max(medias)*0.02,
                        f"{val:.3f}", ha="center", va="bottom", fontsize=9, color=TEXT)

        # ── Speedup relativo al mínimo de hilos ───────────────────────────────
        ax_sp = fig.add_subplot(gs[2, 1])
        ax_sp.set_title("Speedup (relativo a 1 hilo)", color=ACCENT,
                        fontsize=11, pad=8)

        base_idx = hilos_list.index(min(h for h in hilos_list if h > 0)) if any(h > 0 for h in hilos_list) else 0
        base_ms  = medias[base_idx]
        speedups = [base_ms / m for m in medias]

        ax_sp.plot(hilos_list, speedups, color=ACCENT, lw=2, marker="o",
                   markersize=7, zorder=3, label="Speedup medido")

        # Línea de speedup ideal
        h_max = max(hilos_list) if hilos_list else 1
        h_ideal = np.linspace(min(hilos_list) or 1, h_max, 100)
        ax_sp.plot(h_ideal, h_ideal / (min(hilos_list) or 1),
                   color=GRID, lw=1.2, ls="--", label="Speedup ideal (lineal)")

        for hx, sp in zip(hilos_list, speedups):
            ax_sp.annotate(f"{sp:.2f}×", xy=(hx, sp),
                           xytext=(6, 4), textcoords="offset points",
                           fontsize=9, color=ACCENT)

        ax_sp.set_xlabel("Número de hilos")
        ax_sp.set_ylabel("Speedup")
        ax_sp.set_ylim(bottom=0)
        ax_sp.xaxis.set_major_locator(MaxNLocator(integer=True))
        ax_sp.legend(facecolor=PANEL, edgecolor=GRID, labelcolor=TEXT, fontsize=9)

# ─── Guardar y mostrar ────────────────────────────────────────────────────────
out = "wator_resultado.png"
plt.savefig(out, dpi=150, bbox_inches="tight", facecolor=BG)
print(f"  ✅ Guardado en: {out}")
plt.show()