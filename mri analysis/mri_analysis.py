"""
Biomedical Image Processing Pipeline — Brain MRI Segmentation
==============================================================
Simulates a T2-weighted brain MRI, applies multi-stage processing,
segments a tumour-like lesion, and draws anatomical contours.

Pipeline
--------
1. Synthetic MRI generation  (brain + CSF + white matter + lesion)
2. Grayscale conversion
3. CLAHE contrast enhancement
4. Gaussian denoising
5. Multi-level thresholding  (Otsu + manual ROI)
6. Morphological clean-up
7. Contour detection & classification
8. Clinical-style dashboard visualisation
"""

import cv2
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.gridspec as gridspec
from matplotlib.colors import LinearSegmentedColormap

# ── Matplotlib dark theme ─────────────────────────────────────────────────────
plt.rcParams.update({
    "figure.facecolor": "#0d1117",
    "axes.facecolor":   "#0d1117",
    "axes.edgecolor":   "#30363d",
    "axes.labelcolor":  "#c9d1d9",
    "xtick.color":      "#8b949e",
    "ytick.color":      "#8b949e",
    "text.color":       "#c9d1d9",
    "font.family":      "monospace",
    "grid.color":       "#21262d",
    "grid.linestyle":   "--",
    "grid.linewidth":   0.5,
})

BG      = "#0d1117"
PANEL   = "#161b22"
TEAL    = "#00d4aa"
AMBER   = "#ffd166"
CORAL   = "#ff6b6b"
VIOLET  = "#a78bfa"
WHITE   = "#e6edf3"
GREY    = "#8b949e"

CMAP_MRI    = "bone"          # classic radiology look
CMAP_HEAT   = LinearSegmentedColormap.from_list(
    "bio_heat", ["#0d1117", "#1a3a5c", "#0077b6", "#00b4d8",
                  "#90e0ef", "#ffd166", "#ff6b6b"])


# ═══════════════════════════════════════════════════════════════════════════════
# 1.  SYNTHETIC BRAIN MRI  (T2-weighted appearance)
# ═══════════════════════════════════════════════════════════════════════════════

def make_ellipse_mask(shape, cx, cy, rx, ry, angle_deg=0):
    """Return a boolean mask for a rotated ellipse."""
    H, W = shape
    Y, X = np.ogrid[:H, :W]
    angle = np.deg2rad(angle_deg)
    Xr =  (X - cx) * np.cos(angle) + (Y - cy) * np.sin(angle)
    Yr = -(X - cx) * np.sin(angle) + (Y - cy) * np.cos(angle)
    return (Xr / rx) ** 2 + (Yr / ry) ** 2 <= 1.0


def synthesise_brain_mri(size=512, seed=42):
    """
    Generate a realistic-looking T2-weighted axial brain slice.

    Tissue intensities (T2 scale 0-255):
        Background  :   0
        Skull       :  40 – 60
        White matter: 110 – 140
        Grey matter : 150 – 180
        CSF / fluid : 210 – 245   ← bright on T2
        Tumour      : 190 – 220   ← hyperintense on T2
    """
    rng = np.random.default_rng(seed)
    H = W = size
    img = np.zeros((H, W), dtype=np.float32)

    cx, cy = W // 2, H // 2

    # ── Skull outer boundary ─────────────────────────────────────────────────
    skull_mask = make_ellipse_mask((H, W), cx, cy, 215, 190, angle_deg=5)
    img[skull_mask] = 50

    # ── Brain parenchyma (grey matter shell) ──────────────────────────────────
    gm_mask = make_ellipse_mask((H, W), cx, cy, 195, 172, angle_deg=5)
    img[gm_mask] = rng.uniform(150, 180, (H, W)).astype(np.float32)[gm_mask]

    # ── White matter core ─────────────────────────────────────────────────────
    wm_mask = make_ellipse_mask((H, W), cx, cy, 155, 135, angle_deg=5)
    img[wm_mask] = rng.uniform(110, 140, (H, W)).astype(np.float32)[wm_mask]

    # ── Lateral ventricles (CSF — bright) ────────────────────────────────────
    lv_left  = make_ellipse_mask((H, W), cx - 48, cy - 10, 28, 18, angle_deg=-15)
    lv_right = make_ellipse_mask((H, W), cx + 48, cy - 10, 28, 18, angle_deg=15)
    for lv in (lv_left, lv_right):
        img[lv] = rng.uniform(210, 245, (H, W)).astype(np.float32)[lv]

    # ── Third ventricle ───────────────────────────────────────────────────────
    v3_mask = make_ellipse_mask((H, W), cx, cy - 5, 8, 22)
    img[v3_mask] = rng.uniform(215, 240, (H, W)).astype(np.float32)[v3_mask]

    # ── Sulci / fissures  (dark grooves on surface) ───────────────────────────
    mid_fissure = make_ellipse_mask((H, W), cx, cy, 195, 172, angle_deg=5)
    mid_strip   = np.abs(np.arange(W) - cx) < 4
    img[mid_fissure & mid_strip[:, np.newaxis].T] = 30

    # ── Tumour lesion (right frontal, hyperintense + necrotic core) ───────────
    t_cx, t_cy = cx + 80, cy - 55          # right frontal lobe
    tumour_outer = make_ellipse_mask((H, W), t_cx, t_cy, 36, 30, angle_deg=20)
    tumour_inner = make_ellipse_mask((H, W), t_cx, t_cy, 18, 14, angle_deg=20)  # necrotic core
    img[tumour_outer] = rng.uniform(185, 220, (H, W)).astype(np.float32)[tumour_outer]
    img[tumour_inner] = rng.uniform(55,  85,  (H, W)).astype(np.float32)[tumour_inner]  # dark core

    # ── Peri-tumoural oedema (T2 bright ring) ────────────────────────────────
    oedema = make_ellipse_mask((H, W), t_cx + 4, t_cy + 4, 52, 44, angle_deg=22)
    edema_only = oedema & ~tumour_outer & wm_mask
    img[edema_only] = rng.uniform(168, 195, (H, W)).astype(np.float32)[edema_only]

    # ── Rician-like MRI noise ─────────────────────────────────────────────────
    noise_sigma = 4.5
    n1 = rng.normal(0, noise_sigma, (H, W)).astype(np.float32)
    n2 = rng.normal(0, noise_sigma, (H, W)).astype(np.float32)
    rician = np.sqrt((img + n1) ** 2 + n2 ** 2)

    # ── Intensity non-uniformity bias field ──────────────────────────────────
    Y_g, X_g = np.mgrid[0:H, 0:W]
    bias = 1.0 + 0.08 * np.sin(np.pi * X_g / W) * np.cos(np.pi * Y_g / H)
    mri = (rician * bias).clip(0, 255).astype(np.uint8)

    # Store ground-truth masks for evaluation
    masks = {
        "skull":   skull_mask & ~gm_mask,
        "gm":      gm_mask & ~wm_mask,
        "wm":      wm_mask & ~lv_left & ~lv_right & ~v3_mask & ~tumour_outer,
        "csf":     lv_left | lv_right | v3_mask,
        "tumour":  tumour_outer,
        "oedema":  edema_only,
    }
    return mri, masks, (t_cx, t_cy)


# ═══════════════════════════════════════════════════════════════════════════════
# 2.  PRE-PROCESSING
# ═══════════════════════════════════════════════════════════════════════════════

def preprocess(gray_img):
    """CLAHE contrast enhancement + Gaussian smoothing."""
    clahe   = cv2.createCLAHE(clipLimit=2.5, tileGridSize=(8, 8))
    enhanced = clahe.apply(gray_img)
    denoised = cv2.GaussianBlur(enhanced, (5, 5), sigmaX=1.2)
    return enhanced, denoised


# ═══════════════════════════════════════════════════════════════════════════════
# 3.  SEGMENTATION
# ═══════════════════════════════════════════════════════════════════════════════

def segment_brain(denoised):
    """
    Multi-stage thresholding to isolate anatomical regions.

    Returns
    -------
    otsu_mask      : Otsu global threshold (brain vs background)
    thresh_bright  : High-intensity mask → CSF + tumour
    thresh_mid     : Mid-intensity mask  → grey + white matter
    tumour_mask    : Isolated tumour ROI after morphology
    """
    # ── Global Otsu ───────────────────────────────────────────────────────────
    _, otsu_mask = cv2.threshold(denoised, 0, 255,
                                  cv2.THRESH_BINARY + cv2.THRESH_OTSU)

    # ── Bright structures (CSF / tumour): top 20 % of intensities ────────────
    t_bright = int(np.percentile(denoised[otsu_mask > 0], 80))
    _, thresh_bright = cv2.threshold(denoised, t_bright, 255, cv2.THRESH_BINARY)

    # ── Mid-intensity (parenchyma) ────────────────────────────────────────────
    t_lo = int(np.percentile(denoised[otsu_mask > 0], 20))
    t_hi = int(np.percentile(denoised[otsu_mask > 0], 70))
    thresh_mid = cv2.inRange(denoised, t_lo, t_hi)

    # ── Adaptive threshold for fine lesion edges ──────────────────────────────
    adaptive = cv2.adaptiveThreshold(
        denoised, 255,
        cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
        cv2.THRESH_BINARY, 31, -2
    )

    # ── Morphological clean-up on bright mask ────────────────────────────────
    k_open  = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
    k_close = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))
    cleaned = cv2.morphologyEx(thresh_bright, cv2.MORPH_OPEN,  k_open)
    cleaned = cv2.morphologyEx(cleaned,       cv2.MORPH_CLOSE, k_close)

    # ── Isolate tumour: remove ventricles (central, symmetric) ────────────────
    H, W    = denoised.shape
    cx, cy  = W // 2, H // 2
    # ventricle exclusion zone: central horizontal band
    vent_mask = np.zeros((H, W), dtype=np.uint8)
    cv2.ellipse(vent_mask, (cx, cy - 8), (80, 55), 0, 0, 360, 255, -1)

    # Tumour candidate = bright & outside ventricle zone
    tumour_cand = cv2.bitwise_and(cleaned,
                                   cv2.bitwise_not(vent_mask))

    # Keep only regions in right hemisphere (x > cx)
    right_half = np.zeros((H, W), dtype=np.uint8)
    right_half[:, cx:] = 255
    tumour_mask = cv2.bitwise_and(tumour_cand, right_half)

    # Final morphology
    k_t = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))
    tumour_mask = cv2.morphologyEx(tumour_mask, cv2.MORPH_CLOSE, k_t)
    tumour_mask = cv2.morphologyEx(tumour_mask, cv2.MORPH_OPEN,  k_t)

    return otsu_mask, thresh_bright, thresh_mid, cleaned, tumour_mask, adaptive


# ═══════════════════════════════════════════════════════════════════════════════
# 4.  CONTOUR DETECTION & CLASSIFICATION
# ═══════════════════════════════════════════════════════════════════════════════

def find_and_classify_contours(mask, min_area=200):
    """
    Detect contours and classify each by area.

    Returns list of dicts with keys: contour, area, centroid, bbox, label
    """
    contours, hierarchy = cv2.findContours(
        mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
    )
    regions = []
    for cnt in contours:
        area = cv2.contourArea(cnt)
        if area < min_area:
            continue
        M     = cv2.moments(cnt)
        cx    = int(M["m10"] / M["m00"]) if M["m00"] else 0
        cy    = int(M["m01"] / M["m00"]) if M["m00"] else 0
        x, y, w, h = cv2.boundingRect(cnt)
        peri  = cv2.arcLength(cnt, True)
        circ  = (4 * np.pi * area / peri ** 2) if peri > 0 else 0

        if area > 5000:
            label = "CSF/Ventricle"
        elif 800 < area <= 5000 and circ > 0.45:
            label = "Lesion/Tumour"
        else:
            label = "Parenchyma"

        regions.append(dict(contour=cnt, area=area,
                            centroid=(cx, cy), bbox=(x, y, w, h),
                            circularity=circ, label=label))
    return sorted(regions, key=lambda r: r["area"], reverse=True)


def draw_annotated_overlay(base_bgr, regions, tumour_mask):
    """Render coloured contours + labels on a copy of the image."""
    overlay = base_bgr.copy()
    label_colors = {
        "CSF/Ventricle":  (255, 220, 100),   # amber
        "Lesion/Tumour":  (0,   80,  255),   # red-ish (BGR)
        "Parenchyma":     (0,  212, 170),    # teal
    }

    # Tumour fill (semi-transparent red)
    colour_fill = overlay.copy()
    cv2.drawContours(colour_fill,
                     [r["contour"] for r in regions if r["label"] == "Lesion/Tumour"],
                     -1, (0, 60, 220), -1)
    cv2.addWeighted(colour_fill, 0.30, overlay, 0.70, 0, overlay)

    for r in regions:
        col = label_colors[r["label"]]
        thickness = 3 if r["label"] == "Lesion/Tumour" else 2
        cv2.drawContours(overlay, [r["contour"]], -1, col, thickness)

        if r["label"] == "Lesion/Tumour":
            cx, cy = r["centroid"]
            x, y, w, h = r["bbox"]
            cv2.rectangle(overlay, (x - 4, y - 4),
                          (x + w + 4, y + h + 4), (0, 80, 255), 1)
            px_area = r["area"]
            label_str = f"LESION  {px_area} px²"
            (tw, th), _ = cv2.getTextSize(label_str,
                                           cv2.FONT_HERSHEY_SIMPLEX, 0.45, 1)
            cv2.rectangle(overlay,
                          (cx - tw // 2 - 4, cy - th - 10),
                          (cx + tw // 2 + 4, cy - 2), (0, 0, 0), -1)
            cv2.putText(overlay, label_str,
                        (cx - tw // 2, cy - 4),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.45, (60, 220, 255), 1,
                        cv2.LINE_AA)

    return overlay


# ═══════════════════════════════════════════════════════════════════════════════
# 5.  VISUALISATION DASHBOARD
# ═══════════════════════════════════════════════════════════════════════════════

def render_dashboard(mri_gray, enhanced, denoised,
                     otsu_mask, thresh_bright, thresh_mid,
                     tumour_mask, annotated_bgr,
                     regions, gt_masks, adaptive):

    fig = plt.figure(figsize=(20, 14))
    fig.patch.set_facecolor(BG)

    gs = gridspec.GridSpec(
        3, 4,
        figure=fig,
        hspace=0.42, wspace=0.18,
        left=0.04, right=0.96,
        top=0.90,  bottom=0.04,
    )

    axes = [fig.add_subplot(gs[r, c]) for r in range(3) for c in range(4)]
    for ax in axes:
        ax.set_facecolor(PANEL)
        ax.set_xticks([])
        ax.set_yticks([])
        for sp in ax.spines.values():
            sp.set_edgecolor("#30363d")

    def title(ax, txt, color=WHITE):
        ax.set_title(txt, fontsize=9, color=color, pad=5, loc="left",
                     fontweight="bold")

    def subtitle(ax, txt):
        ax.text(0.99, 0.02, txt, transform=ax.transAxes, fontsize=7,
                color=GREY, ha="right", va="bottom",
                bbox=dict(boxstyle="round,pad=0.2", fc="#0d1117", alpha=0.6))

    # 0 — Raw synthetic MRI
    axes[0].imshow(mri_gray, cmap=CMAP_MRI, vmin=0, vmax=255)
    title(axes[0], "A — Synthetic T2 Brain MRI", WHITE)
    subtitle(axes[0], "Rician noise · bias field")

    # 1 — CLAHE enhanced
    axes[1].imshow(enhanced, cmap=CMAP_MRI, vmin=0, vmax=255)
    title(axes[1], "B — CLAHE Enhanced", TEAL)
    subtitle(axes[1], "clipLimit=2.5 · 8×8 tiles")

    # 2 — Gaussian denoised
    axes[2].imshow(denoised, cmap=CMAP_MRI, vmin=0, vmax=255)
    title(axes[2], "C — Gaussian Denoised", TEAL)
    subtitle(axes[2], "5×5 kernel · σ=1.2")

    # 3 — Intensity histogram
    axes[3].set_facecolor(PANEL)
    axes[3].tick_params(colors=GREY, labelsize=7)
    for sp in axes[3].spines.values():
        sp.set_edgecolor("#30363d")
    hist_brain = mri_gray[mri_gray > 10].ravel()
    n, bins, patches = axes[3].hist(hist_brain, bins=80, color=TEAL,
                                     alpha=0.75, edgecolor="none")
    # Colour regions
    for patch, left in zip(patches, bins[:-1]):
        if   left < 60:  patch.set_facecolor("#30363d")
        elif left < 105: patch.set_facecolor(VIOLET)
        elif left < 155: patch.set_facecolor(TEAL)
        elif left < 200: patch.set_facecolor(AMBER)
        else:            patch.set_facecolor(CORAL)
    for label, x, col in [("WM", 122, VIOLET), ("GM", 162, TEAL),
                            ("CSF", 228, CORAL), ("Lesion", 202, AMBER)]:
        axes[3].axvline(x, color=col, lw=1, ls="--", alpha=0.7)
        axes[3].text(x + 2, n.max() * 0.65, label,
                     color=col, fontsize=7, rotation=90)
    title(axes[3], "D — Intensity Histogram", WHITE)
    axes[3].set_xlabel("Intensity", fontsize=7, color=GREY)
    axes[3].set_ylabel("Count",     fontsize=7, color=GREY)
    axes[3].set_xlim(0, 255)
    axes[3].grid(True, axis="y", alpha=0.4)

    # 4 — Otsu threshold
    axes[4].imshow(otsu_mask, cmap="gray")
    title(axes[4], "E — Otsu Threshold", AMBER)
    subtitle(axes[4], "Brain / background")

    # 5 — Bright structures
    axes[5].imshow(thresh_bright, cmap="gray")
    title(axes[5], "F — High-Intensity Mask", AMBER)
    subtitle(axes[5], "Top 20 % intensities")

    # 6 — Mid intensity (parenchyma)
    axes[6].imshow(thresh_mid, cmap="gray")
    title(axes[6], "G — Mid-Intensity Mask", AMBER)
    subtitle(axes[6], "Parenchyma range")

    # 7 — Adaptive threshold
    axes[7].imshow(adaptive, cmap="gray")
    title(axes[7], "H — Adaptive Threshold", AMBER)
    subtitle(axes[7], "Gaussian C · block=31")

    # 8 — Tumour mask (isolated)
    axes[8].imshow(tumour_mask, cmap="hot")
    title(axes[8], "I — Tumour Segmentation Mask", CORAL)
    subtitle(axes[8], "Morphology cleaned")

    # 9 — Ground truth overlay
    gt_overlay = np.zeros((*mri_gray.shape, 3), dtype=np.uint8)
    gt_colors = {
        "gm":     (100, 180, 255),
        "wm":     (60,  120, 200),
        "csf":    (255, 210, 80),
        "tumour": (255, 70,  70),
        "oedema": (180, 230, 130),
    }
    for key, col in gt_colors.items():
        gt_overlay[gt_masks[key]] = col
    gt_bg = cv2.cvtColor(mri_gray, cv2.COLOR_GRAY2RGB).astype(np.float32)
    blend = (0.45 * gt_overlay + 0.55 * gt_bg).clip(0, 255).astype(np.uint8)
    axes[9].imshow(blend)
    legend_patches = [
        mpatches.Patch(color=np.array(c)/255, label=k.upper())
        for k, c in gt_colors.items()
    ]
    axes[9].legend(handles=legend_patches, loc="lower left", fontsize=6,
                   framealpha=0.7, facecolor="#0d1117",
                   labelcolor="white", edgecolor="#30363d")
    title(axes[9], "J — Ground Truth Labels", VIOLET)

    # 10 — Annotated contour result (main output)
    ann_rgb = cv2.cvtColor(annotated_bgr, cv2.COLOR_BGR2RGB)
    axes[10].imshow(ann_rgb)
    label_patches = [
        mpatches.Patch(color=np.array([255, 220, 100])/255, label="CSF/Ventricle"),
        mpatches.Patch(color=np.array([255, 80,  60])/255,  label="Lesion/Tumour"),
        mpatches.Patch(color=np.array([0,   212, 170])/255, label="Parenchyma"),
    ]
    axes[10].legend(handles=label_patches, loc="lower left", fontsize=6,
                    framealpha=0.7, facecolor="#0d1117",
                    labelcolor="white", edgecolor="#30363d")
    title(axes[10], "K — Contour Detection & Annotation", CORAL)

    # 11 — Metrics panel
    axes[11].set_facecolor(PANEL)
    axes[11].set_xlim(0, 1)
    axes[11].set_ylim(0, 1)
    axes[11].tick_params(left=False, bottom=False,
                          labelleft=False, labelbottom=False)
    axes[11].set_xlabel("")
    axes[11].set_ylabel("")

    H, W = mri_gray.shape
    total_px  = H * W
    brain_px  = int(np.sum(mri_gray > 10))
    tumour_px = int(np.sum(tumour_mask > 0))
    tumour_vol_frac = tumour_px / brain_px * 100 if brain_px else 0
    n_lesions = sum(1 for r in regions if r["label"] == "Lesion/Tumour")
    n_csf     = sum(1 for r in regions if r["label"] == "CSF/Ventricle")
    mean_int  = float(np.mean(mri_gray[tumour_mask > 0])) if tumour_px else 0

    metrics = [
        ("IMAGE SIZE",          f"{W} × {H} px"),
        ("BRAIN AREA",          f"{brain_px:,} px²"),
        ("TUMOUR AREA",         f"{tumour_px:,} px²"),
        ("TUMOUR / BRAIN",      f"{tumour_vol_frac:.2f} %"),
        ("LESION REGIONS",      f"{n_lesions}"),
        ("CSF REGIONS",         f"{n_csf}"),
        ("TUMOUR MEAN INT.",     f"{mean_int:.1f}"),
        ("TOTAL CONTOURS",      f"{len(regions)}"),
        ("SEGMENTATION METHOD", "Otsu + Adaptive"),
        ("FILTER",              "Gaussian 5×5 σ=1.2"),
        ("MORPH. KERNEL",       "Ellipse 7×7"),
    ]

    y_pos = 0.96
    axes[11].text(0.5, y_pos, "L — Analysis Report",
                  ha="center", va="top", fontsize=9,
                  color=TEAL, fontweight="bold",
                  transform=axes[11].transAxes)
    y_pos -= 0.08
    axes[11].axhline(y_pos, color="#30363d", lw=0.8, xmin=0.02, xmax=0.98)
    y_pos -= 0.02

    for key, val in metrics:
        axes[11].text(0.04, y_pos, key,
                      ha="left", va="top", fontsize=7.5,
                      color=GREY,  transform=axes[11].transAxes)
        axes[11].text(0.96, y_pos, val,
                      ha="right", va="top", fontsize=7.5,
                      color=WHITE, transform=axes[11].transAxes,
                      fontweight="bold")
        y_pos -= 0.075
        if y_pos < 0.05:
            break

    # Tumour indicator badge
    if tumour_px > 0:
        axes[11].text(0.5, 0.05,
                      "⚠  LESION DETECTED",
                      ha="center", va="bottom", fontsize=9,
                      color=CORAL, fontweight="bold",
                      transform=axes[11].transAxes,
                      bbox=dict(boxstyle="round,pad=0.35",
                                fc="#2a0a0a", ec=CORAL, lw=1.2))

    # ── Super-title ───────────────────────────────────────────────────────────
    fig.suptitle(
        "Biomedical Image Processing Pipeline  ·  Brain MRI Segmentation & Contour Detection",
        fontsize=13, color=WHITE, y=0.955, fontweight="bold",
    )

    out = "/mnt/user-data/outputs/mri_analysis.png"
    plt.savefig(out, dpi=150, bbox_inches="tight",
                facecolor=fig.get_facecolor())
    plt.close(fig)
    print(f"  ✔  Dashboard saved → {out}")
    return out


# ═══════════════════════════════════════════════════════════════════════════════
# 6.  MAIN
# ═══════════════════════════════════════════════════════════════════════════════

def main():
    print("=" * 62)
    print("  Biomedical Image Processing Pipeline — Brain MRI")
    print("=" * 62)

    # 1. Synthesise
    print("\n[1] Generating synthetic T2-weighted brain MRI …")
    mri_gray, gt_masks, tumour_centre = synthesise_brain_mri(size=512)
    print(f"    Image size : {mri_gray.shape}  |  dtype: {mri_gray.dtype}")

    # 2. Pre-process
    print("\n[2] Pre-processing (CLAHE + Gaussian denoising) …")
    enhanced, denoised = preprocess(mri_gray)
    print(f"    Contrast range before CLAHE: "
          f"{mri_gray.min()} – {mri_gray.max()}")
    print(f"    Contrast range after  CLAHE: "
          f"{enhanced.min()} – {enhanced.max()}")

    # 3. Segment
    print("\n[3] Segmenting tissue regions …")
    otsu_mask, thresh_bright, thresh_mid, cleaned, tumour_mask, adaptive \
        = segment_brain(denoised)
    tumour_px = int(np.sum(tumour_mask > 0))
    print(f"    Otsu threshold   : {cv2.threshold(denoised, 0, 255, cv2.THRESH_OTSU)[0]:.0f}")
    print(f"    Tumour pixels    : {tumour_px:,}")

    # 4. Contours
    print("\n[4] Detecting contours …")
    regions = find_and_classify_contours(cleaned, min_area=200)
    for r in regions[:6]:
        print(f"    {r['label']:18s}  area={r['area']:6.0f} px²  "
              f"circ={r['circularity']:.2f}  centre={r['centroid']}")

    # 5. Annotate
    print("\n[5] Drawing annotated overlay …")
    base_bgr = cv2.cvtColor(mri_gray, cv2.COLOR_GRAY2BGR)
    annotated = draw_annotated_overlay(base_bgr, regions, tumour_mask)

    # Compute Dice with ground truth
    pred_bin = (tumour_mask > 0).astype(np.uint8)
    gt_bin   = gt_masks["tumour"].astype(np.uint8)
    inter    = int(np.sum(pred_bin & gt_bin))
    union    = int(np.sum(pred_bin) + np.sum(gt_bin))
    dice     = (2 * inter / union) if union > 0 else 0.0
    print(f"\n    Dice coefficient (tumour) : {dice:.3f}")

    # 6. Render
    print("\n[6] Rendering clinical dashboard …")
    render_dashboard(
        mri_gray, enhanced, denoised,
        otsu_mask, thresh_bright, thresh_mid,
        tumour_mask, annotated, regions, gt_masks, adaptive
    )

    print("\n" + "=" * 62)
    print("  Pipeline complete.")
    print("=" * 62)


if __name__ == "__main__":
    main()
