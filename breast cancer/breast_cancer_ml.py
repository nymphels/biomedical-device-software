"""
=============================================================
 Biomedical ML Pipeline — Breast Cancer Diagnosis Classifier
=============================================================
 Dataset : sklearn.datasets.load_breast_cancer
           (569 samples · 30 features · binary labels)
 Models  : Logistic Regression  |  Decision Tree
 Outputs : Accuracy, Classification Report, Confusion Matrix
=============================================================
"""

# ── 1. Imports ────────────────────────────────────────────
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import seaborn as sns

from sklearn.datasets import load_breast_cancer
from sklearn.model_selection import train_test_split, cross_val_score
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import LogisticRegression
from sklearn.tree import DecisionTreeClassifier, plot_tree
from sklearn.metrics import (
    accuracy_score,
    classification_report,
    confusion_matrix,
    ConfusionMatrixDisplay,
    roc_auc_score,
    roc_curve,
)

# ── 2. Load & Explore Dataset ─────────────────────────────
print("=" * 60)
print("  BREAST CANCER WISCONSIN DIAGNOSTIC DATASET")
print("=" * 60)

data   = load_breast_cancer()
X, y   = data.data, data.target
labels = data.target_names          # ['malignant', 'benign']
feats  = data.feature_names

print(f"\n  Samples  : {X.shape[0]}")
print(f"  Features : {X.shape[1]}")
print(f"  Classes  : {labels.tolist()}")
print(f"  Malignant (0): {(y == 0).sum()}  |  Benign (1): {(y == 1).sum()}")

# ── 3. Data Cleaning ──────────────────────────────────────
print("\n[Data Cleaning]")
missing = np.isnan(X).sum()
print(f"  Missing values : {missing}  ← none expected in this dataset")
print("  Feature ranges standardised via StandardScaler")

# ── 4. Train / Test Split ─────────────────────────────────
X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.20, random_state=42, stratify=y
)
print(f"\n  Train samples : {len(X_train)}")
print(f"  Test  samples : {len(X_test)}")

# ── 5. Feature Scaling ────────────────────────────────────
scaler  = StandardScaler()
X_train = scaler.fit_transform(X_train)
X_test  = scaler.transform(X_test)

# ── 6. Model Training ─────────────────────────────────────
models = {
    "Logistic Regression": LogisticRegression(max_iter=10_000, random_state=42),
    "Decision Tree"      : DecisionTreeClassifier(max_depth=4, random_state=42),
}

results = {}
for name, model in models.items():
    model.fit(X_train, y_train)
    y_pred = model.predict(X_test)
    y_prob = (
        model.predict_proba(X_test)[:, 1]
        if hasattr(model, "predict_proba")
        else None
    )
    cv_scores = cross_val_score(model, X_train, y_train, cv=5, scoring="accuracy")

    results[name] = {
        "model"    : model,
        "y_pred"   : y_pred,
        "y_prob"   : y_prob,
        "accuracy" : accuracy_score(y_test, y_pred),
        "roc_auc"  : roc_auc_score(y_test, y_prob) if y_prob is not None else None,
        "cv_mean"  : cv_scores.mean(),
        "cv_std"   : cv_scores.std(),
        "cm"       : confusion_matrix(y_test, y_pred),
        "report"   : classification_report(y_test, y_pred, target_names=labels),
    }

# ── 7. Console Report ─────────────────────────────────────
for name, r in results.items():
    print(f"\n{'─'*60}")
    print(f"  MODEL : {name}")
    print(f"{'─'*60}")
    print(f"  Test Accuracy  : {r['accuracy']:.4f}")
    if r["roc_auc"]:
        print(f"  ROC-AUC Score  : {r['roc_auc']:.4f}")
    print(f"  CV Accuracy    : {r['cv_mean']:.4f} ± {r['cv_std']:.4f}")
    print(f"\n  Classification Report:\n")
    print(r["report"])

# ── 8. Visualisation ──────────────────────────────────────
PALETTE = {
    "bg"      : "#0f1117",
    "panel"   : "#1a1d27",
    "accent1" : "#4fc3f7",   # cyan  – benign
    "accent2" : "#ef5350",   # red   – malignant
    "text"    : "#e0e0e0",
    "subtext" : "#9e9e9e",
}

plt.rcParams.update({
    "figure.facecolor" : PALETTE["bg"],
    "axes.facecolor"   : PALETTE["panel"],
    "axes.edgecolor"   : "#2e3145",
    "axes.labelcolor"  : PALETTE["text"],
    "xtick.color"      : PALETTE["subtext"],
    "ytick.color"      : PALETTE["subtext"],
    "text.color"       : PALETTE["text"],
    "grid.color"       : "#2e3145",
    "grid.linewidth"   : 0.6,
    "font.family"      : "DejaVu Sans",
})

fig = plt.figure(figsize=(18, 13))
fig.suptitle(
    "Breast Cancer Diagnosis — ML Classification Report",
    fontsize=16, fontweight="bold", color=PALETTE["text"], y=0.98,
)
gs = gridspec.GridSpec(2, 3, figure=fig, hspace=0.42, wspace=0.35)

model_list = list(results.items())

# ── 8a. Confusion Matrices ────────────────────────────────
cm_colours = ["#ef5350", "#4fc3f7", "#4fc3f7", "#26a69a"]   # TN FP FN TP

for col, (name, r) in enumerate(model_list):
    ax = fig.add_subplot(gs[0, col])
    cm = r["cm"]

    im = ax.imshow(cm, cmap="Blues", aspect="auto")
    ax.set_xticks([0, 1]); ax.set_yticks([0, 1])
    ax.set_xticklabels(labels, fontsize=10)
    ax.set_yticklabels(labels, fontsize=10, rotation=45, va="center")
    ax.set_xlabel("Predicted Label", fontsize=10, labelpad=8)
    ax.set_ylabel("True Label",      fontsize=10, labelpad=8)
    ax.set_title(f"{name}\nConfusion Matrix", fontsize=11,
                 fontweight="bold", color=PALETTE["accent1"], pad=10)

    thresh = cm.max() / 2
    for i in range(2):
        for j in range(2):
            ax.text(j, i, cm[i, j], ha="center", va="center",
                    fontsize=22, fontweight="bold",
                    color="white" if cm[i, j] > thresh else PALETTE["text"])

# ── 8b. ROC Curves (both on one axes) ────────────────────
ax_roc = fig.add_subplot(gs[0, 2])
ax_roc.plot([0, 1], [0, 1], "--", color=PALETTE["subtext"], linewidth=1, label="Chance")
roc_colours = [PALETTE["accent1"], "#ffb74d"]
for (name, r), colour in zip(model_list, roc_colours):
    if r["y_prob"] is not None:
        fpr, tpr, _ = roc_curve(y_test, r["y_prob"])
        ax_roc.plot(fpr, tpr, colour, linewidth=2,
                    label=f"{name.split()[0]} (AUC={r['roc_auc']:.3f})")
ax_roc.set_xlabel("False Positive Rate", fontsize=10)
ax_roc.set_ylabel("True Positive Rate",  fontsize=10)
ax_roc.set_title("ROC Curves", fontsize=11, fontweight="bold",
                 color=PALETTE["accent1"], pad=10)
ax_roc.legend(fontsize=8, facecolor=PALETTE["panel"],
              edgecolor="#2e3145", labelcolor=PALETTE["text"])
ax_roc.grid(True)

# ── 8c. Feature Importance (Decision Tree) ───────────────
ax_feat = fig.add_subplot(gs[1, 0:2])
dt_model    = results["Decision Tree"]["model"]
importances = dt_model.feature_importances_
top_n       = 10
top_idx     = np.argsort(importances)[-top_n:][::-1]
top_feats   = [feats[i] for i in top_idx]
top_vals    = importances[top_idx]

bar_colours = [
    PALETTE["accent1"] if v == top_vals.max() else "#4dd0e1"
    for v in top_vals
]
bars = ax_feat.barh(range(top_n), top_vals[::-1], color=bar_colours[::-1],
                    edgecolor="none", height=0.65)
ax_feat.set_yticks(range(top_n))
ax_feat.set_yticklabels(top_feats[::-1], fontsize=9)
ax_feat.set_xlabel("Gini Importance", fontsize=10)
ax_feat.set_title("Top 10 Feature Importances — Decision Tree",
                  fontsize=11, fontweight="bold", color=PALETTE["accent1"], pad=10)
ax_feat.grid(True, axis="x")

# ── 8d. Accuracy / CV Score Bar Chart ────────────────────
ax_acc = fig.add_subplot(gs[1, 2])
names  = [n.replace(" ", "\n") for n in results]
accs   = [r["accuracy"]  for r in results.values()]
cvs    = [r["cv_mean"]   for r in results.values()]
x      = np.arange(len(names))
w      = 0.32

b1 = ax_acc.bar(x - w/2, accs, w, label="Test Accuracy",
                color=PALETTE["accent1"], edgecolor="none", alpha=0.9)
b2 = ax_acc.bar(x + w/2, cvs,  w, label="CV Accuracy (5-fold)",
                color="#ffb74d",          edgecolor="none", alpha=0.9)

for bar in list(b1) + list(b2):
    ax_acc.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.003,
                f"{bar.get_height():.3f}", ha="center", va="bottom",
                fontsize=9, color=PALETTE["text"])

ax_acc.set_xticks(x); ax_acc.set_xticklabels(names, fontsize=9)
ax_acc.set_ylim(0.88, 1.02)
ax_acc.set_ylabel("Accuracy", fontsize=10)
ax_acc.set_title("Model Performance Summary",
                 fontsize=11, fontweight="bold", color=PALETTE["accent1"], pad=10)
ax_acc.legend(fontsize=8, facecolor=PALETTE["panel"],
              edgecolor="#2e3145", labelcolor=PALETTE["text"])
ax_acc.grid(True, axis="y")

plt.savefig("breast_cancer_ml_report.png", dpi=150,
            bbox_inches="tight", facecolor=PALETTE["bg"])
print("\n  Figure saved → breast_cancer_ml_report.png")
plt.show()
print("\n  Done.")
