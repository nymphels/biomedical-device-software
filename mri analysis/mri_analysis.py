import cv2
import numpy as np
import matplotlib.pyplot as plt

# ============================================================
# STEP 1 — Load the image & convert to grayscale
# ============================================================
# pretty basic stuff, just loading the image first
# make sure the file is in the same folder as this script!

img = cv2.imread("mri_brain.jpg")

# always good to check if it actually loaded, spent 10 min debugging this once lol
if img is None:
    print("ERROR: couldn't load image. check the filename / path!")
    exit()

# opencv loads as BGR by default, need RGB for matplotlib later
img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)

# grayscale version — most processing steps need this
gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

print(f"image loaded! size: {gray.shape[1]} x {gray.shape[0]} px")

# quick look at what we're working with
plt.figure(figsize=(10, 4))

plt.subplot(1, 2, 1)
plt.imshow(img_rgb)
plt.title("original image (color)")
plt.axis("off")

plt.subplot(1, 2, 2)
plt.imshow(gray, cmap="gray")
plt.title("grayscale version")
plt.axis("off")

plt.tight_layout()
plt.savefig("step1_load_gray.png", dpi=120)
plt.show()
print("step 1 done — saved step1_load_gray.png")


# ============================================================
# STEP 2 — Preprocessing + Thresholding
# ============================================================
# goal here: separate brain tissue from background
# tried a few methods, notes below on what worked and what didn't

# --- 2a. denoise first, otherwise thresholding gets really noisy ---
# gaussian blur smooths out the speckle noise in MRI
# kernel size (5,5) worked ok for me, try (3,3) for less blurring
# or (7,7) if the image is really noisy
blurred = cv2.GaussianBlur(gray, (5, 5), sigmaX=1.2)

# --- 2b. Otsu thresholding — automatically picks the threshold value ---
# honestly this is way better than guessing a manual value like 127
# the second return value is the threshold it chose, useful to print
otsu_thresh_val, otsu_mask = cv2.threshold(
    blurred, 0, 255,
    cv2.THRESH_BINARY + cv2.THRESH_OTSU  # the OTSU flag makes it auto-pick
)
print(f"otsu picked threshold = {otsu_thresh_val:.1f}  (0-255 range)")

# --- 2c. Adaptive threshold — works better when lighting is uneven ---
# this one looks at small local patches instead of the whole image at once
# blockSize must be ODD (11, 15, 21...), C is a constant subtracted from mean
# TODO: tweak blockSize — larger = smoother regions, smaller = more detail
adaptive_mask = cv2.adaptiveThreshold(
    blurred, 255,
    cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
    cv2.THRESH_BINARY,
    blockSize=11,   # <-- play with this
    C=2             # <-- and this
)

# --- 2d. clean up the mask a bit with morphology ---
# small white specks = noise → erode removes them
# then dilate brings back the main regions we actually want
# ellipse kernel feels more natural for rounded brain shapes vs a rectangle
morph_kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))
cleaned_mask = cv2.morphologyEx(otsu_mask, cv2.MORPH_OPEN, morph_kernel)

# visualise all the threshold results side by side
fig, axes = plt.subplots(1, 3, figsize=(14, 4))
fig.suptitle("Step 2 — Thresholding", fontsize=13)

axes[0].imshow(otsu_mask, cmap="gray")
axes[0].set_title(f"otsu (auto thresh={otsu_thresh_val:.0f})")
axes[0].axis("off")

axes[1].imshow(adaptive_mask, cmap="gray")
axes[1].set_title("adaptive (local)")
axes[1].axis("off")

axes[2].imshow(cleaned_mask, cmap="gray")
axes[2].set_title("otsu + morphology clean")
axes[2].axis("off")

plt.tight_layout()
plt.savefig("step2_thresholding.png", dpi=120)
plt.show()
print("step 2 done — saved step2_thresholding.png")


# ============================================================
# STEP 3 — Contour Detection & Annotation
# ============================================================
# now we find the outlines of the regions in the cleaned mask
# contours are basically lists of (x, y) points along the boundary

# cv2.findContours needs a binary image — use cleaned_mask from step 2
# RETR_EXTERNAL: only outermost contours (ignore holes inside shapes)
# RETR_TREE: full hierarchy including inner contours — good for nested regions
# CHAIN_APPROX_SIMPLE: compresses straight segments (saves memory)
# TODO: switch to RETR_TREE later if I want to detect ventricles inside the brain
contours, hierarchy = cv2.findContours(
    cleaned_mask,
    cv2.RETR_EXTERNAL,        # <-- try RETR_TREE to also detect inner rings
    cv2.CHAIN_APPROX_SIMPLE
)

print(f"found {len(contours)} total contours")

# filter out tiny contours — mostly just noise
# min_area is in pixels^2, might need to adjust depending on image resolution
min_area = 300   # TODO: lower this if small lesions are being missed
big_contours = [c for c in contours if cv2.contourArea(c) > min_area]
print(f"  → {len(big_contours)} contours kept after filtering (area > {min_area} px²)")

# draw contours on a copy of the original image
annotated = img_rgb.copy()

for i, contour in enumerate(big_contours):
    area = cv2.contourArea(contour)

    # colour-code by size — big = likely brain boundary, small = lesion candidate
    if area > 50000:
        color = (0, 200, 255)   # cyan — probably the outer brain/skull boundary
        label = "BRAIN"
    elif area > 5000:
        color = (255, 165, 0)   # orange — could be a tissue region / parenchyma
        label = "REGION"
    else:
        color = (255, 80, 80)   # red — small enough to be a lesion candidate
        label = "LESION"

    cv2.drawContours(annotated, [contour], -1, color, 2)

    # put the area label near the centroid of each contour
    # moments() gives us the centroid — M["m10"]/M["m00"] = x_center
    M = cv2.moments(contour)
    if M["m00"] != 0:  # avoid divide-by-zero for weird edge contours
        cx = int(M["m10"] / M["m00"])
        cy = int(M["m01"] / M["m00"])
        cv2.putText(
            annotated,
            f"{label} {area:.0f}px",
            (cx - 30, cy),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.4,        # font scale — adjust if text is too big/small
            color, 1,
            cv2.LINE_AA
        )

# --- final summary plot ---
fig, axes = plt.subplots(1, 3, figsize=(15, 5))
fig.suptitle("Step 3 — Contour Detection", fontsize=13)

axes[0].imshow(gray, cmap="gray")
axes[0].set_title("original grayscale")
axes[0].axis("off")

axes[1].imshow(cleaned_mask, cmap="gray")
axes[1].set_title("final binary mask")
axes[1].axis("off")

axes[2].imshow(annotated)
axes[2].set_title(f"contours annotated ({len(big_contours)} regions)")
axes[2].axis("off")

plt.tight_layout()
plt.savefig("step3_contours.png", dpi=120)
plt.show()
print("step 3 done — saved step3_contours.png")

# ============================================================
# quick summary printout
# ============================================================
print("\n--- analysis summary ---")
print(f"  image size      : {gray.shape[1]} x {gray.shape[0]}")
print(f"  otsu threshold  : {otsu_thresh_val:.1f}")
print(f"  total contours  : {len(contours)}")
print(f"  after filtering : {len(big_contours)}")
print("done!")
