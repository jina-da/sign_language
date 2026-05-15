import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
import matplotlib
matplotlib.rcParams['font.family'] = 'DejaVu Sans'
matplotlib.rcParams['figure.facecolor'] = 'white'
matplotlib.rcParams['axes.facecolor'] = '#FAFAFA'
matplotlib.rcParams['axes.spines.top'] = False
matplotlib.rcParams['axes.spines.right'] = False
import os
os.makedirs('portfolio_images', exist_ok=True)

# 1. Version Accuracy
fig, ax = plt.subplots(figsize=(11, 5))
versions = ['v1\nbaseline', 'v2\nepoch100', 'v3\naugment', 'v4\nfactor0.7', 'v5\nhidden512', 'v6\njoint\norder', 'v7\ndelta']
accs = [79.42, 85.73, 89.96, 91.29, 91.48, 93.18, 93.69]
colors = ['#85B7EB']*5 + ['#1D9E75', '#0F6E56']
bars = ax.bar(versions, accs, color=colors, width=0.6, zorder=3)
for bar, acc in zip(bars, accs):
    ax.text(bar.get_x()+bar.get_width()/2, bar.get_height()+0.2,
            f'{acc:.2f}%', ha='center', va='bottom', fontsize=10, fontweight='bold')
ax.set_ylim(75, 97)
ax.set_ylabel('Test Accuracy (%)', fontsize=12)
ax.set_title('SignLearn AI — Model Performance Improvement by Version', fontsize=14, fontweight='bold', pad=15)
ax.yaxis.grid(True, alpha=0.4, zorder=0)
ax.set_axisbelow(True)
ax.legend(handles=[
    mpatches.Patch(color='#85B7EB', label='General improvement'),
    mpatches.Patch(color='#1D9E75', label='Joint order fix (v6)'),
    mpatches.Patch(color='#0F6E56', label='Delta feature (v7)'),
], loc='lower right', fontsize=10)
plt.tight_layout()
plt.savefig('portfolio_images/01_version_accuracy.png', dpi=150, bbox_inches='tight')
plt.close()
print("01 done")

# 2. Training Curve (v7)
fig, ax = plt.subplots(figsize=(11, 5))
epochs = list(range(1, 101))
val_acc  = [7.89,28.79,52.71,65.15,74.68,78.35,80.93,82.83,87.25,88.38,88.26,88.89,87.44,88.38,88.19,90.28,90.66,90.59,90.66,89.96,90.78,89.96,91.04,91.22,91.79,90.91,91.04,90.03,91.22,91.86,91.29,91.35,91.22,91.10,91.04,91.79,91.92,92.05,91.67,91.10,90.72,91.98,91.79,91.79,91.48,91.98,91.60,91.48,91.98,91.92,91.79,91.98,91.73,92.05,91.67,91.98,92.23,92.61,92.05,91.92,91.92,92.30,92.05,91.73,91.98,92.30,92.30,92.17,92.42,92.42,92.36,92.17,92.30,91.92,92.36,92.30,92.05,92.11,92.11,92.23,92.30,91.98,92.11,92.11,92.30,92.11,92.05,92.11,92.23,92.61,92.61,92.36,92.11,92.11,92.11,92.30,92.36,92.30,92.17,92.30]
train_acc= [2.08,14.23,34.95,54.43,69.03,78.75,85.48,89.48,92.00,93.54,95.13,95.75,96.65,97.21,97.60,97.85,98.87,99.00,99.12,99.13,99.30,99.39,99.67,99.68,99.69,99.74,99.74,99.76,99.89,99.88,99.90,99.87,99.88,99.87,99.94,99.93,99.96,99.96,99.95,99.94,99.97,99.99,99.98,99.97,99.98,99.99,99.99,99.99,99.99,99.99,99.99,99.99,100.0,99.99,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0,100.0]
ax.plot(epochs, val_acc,   color='#378ADD', linewidth=2.5, label='Val Accuracy')
ax.plot(epochs, train_acc, color='#1D9E75', linewidth=1.5, linestyle='--', label='Train Accuracy')
ax.fill_between(epochs, val_acc, alpha=0.08, color='#378ADD')
ax.axhline(y=93.69, color='#D85A30', linestyle=':', linewidth=1.5, label='Final test acc 93.69%')
ax.set_xlabel('Epoch', fontsize=12)
ax.set_ylabel('Accuracy (%)', fontsize=12)
ax.set_title('SignLearn AI v7 — Training Curve (Coord + Delta Features, input_dim=268)', fontsize=13, fontweight='bold', pad=15)
ax.legend(fontsize=10, loc='lower right')
ax.yaxis.grid(True, alpha=0.4)
ax.set_axisbelow(True)
plt.tight_layout()
plt.savefig('portfolio_images/02_training_curve.png', dpi=150, bbox_inches='tight')
plt.close()
print("02 done")

# 3. Joint Order Mismatch
fig, axes = plt.subplots(1, 2, figsize=(12, 7))
op_labels = ['Nose(0)','Neck(1)','RShoulder(2)','RElbow(3)','RWrist(4)',
             'LShoulder(5)','LElbow(6)','LWrist(7)','MidHip(8)','RHip(9)',
             'RKnee(10)','RAnkle(11)','LHip(12)','LKnee(13)','LAnkle(14)']
mp_labels = ['Nose(0)','L.eye inner(1)','L.eye(2)','L.eye outer(3)','R.eye inner(4)',
             'R.eye(5)','R.eye outer(6)','L.ear(7)','R.ear(8)','Mouth L(9)',
             'Mouth R(10)','L.shoulder(11)','R.shoulder(12)','L.elbow(13)','R.elbow(14)']
match = [True]+[False]*14
for ax, labels, title, ec in zip(axes, [op_labels, mp_labels],
                                  ['OpenPose (Training Data)', 'MediaPipe (Client Input)'],
                                  ['#E24B4A', '#378ADD']):
    for i, (label, m) in enumerate(zip(labels, match)):
        y = len(labels)-1-i
        fc = '#E6FBF3' if m else ('#FEF0EE' if ec=='#E24B4A' else '#EEF4FD')
        lw = 2.5 if m else 1
        rect = plt.Rectangle((0.05, y+0.1), 0.9, 0.75,
                              facecolor=fc, edgecolor='#1D9E75' if m else ec,
                              linewidth=lw, transform=ax.transData)
        ax.add_patch(rect)
        ax.text(0.5, y+0.47, label, ha='center', va='center', fontsize=9,
                color='#0C447C' if ec=='#378ADD' else '#791F1F',
                fontweight='bold' if m else 'normal')
    ax.set_xlim(0,1); ax.set_ylim(-0.3, len(labels)); ax.axis('off')
    ax.set_title(title, fontsize=12, fontweight='bold', color=ec, pad=10)
fig.text(0.5, 0.01, 'Only index 0 (Nose) matches — all others differ → model learned wrong joint positions',
         ha='center', fontsize=11, color='#D85A30', fontweight='bold')
fig.suptitle('Joint Order Mismatch: OpenPose vs MediaPipe (Pose index 0~14)', fontsize=14, fontweight='bold', y=1.02)
plt.tight_layout()
plt.savefig('portfolio_images/03_joint_order_mismatch.png', dpi=150, bbox_inches='tight')
plt.close()
print("03 done")

# 4. False Positive Analysis
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))
samples = ['Sample1','Sample2','Sample3','Sample4','Sample5','Mean']
sims    = [0.8051,0.8021,0.8030,0.5342,0.7817,0.7452]
bcolors = ['#85B7EB']*3+['#E24B4A']+['#85B7EB']+['#378ADD']
b1 = ax1.bar(samples, sims, color=bcolors, width=0.6, zorder=3)
for bar, s in zip(b1, sims):
    ax1.text(bar.get_x()+bar.get_width()/2, bar.get_height()+0.01,
             f'{s:.4f}', ha='center', va='bottom', fontsize=9, fontweight='bold')
ax1.axhline(0.85, color='#1D9E75', linestyle='--', lw=1.5, label='Acceptable (0.85)')
ax1.axhline(0.95, color='#0F6E56', linestyle=':',  lw=1.5, label='Normal (0.95)')
ax1.set_ylim(0,1.1); ax1.set_ylabel('Cosine Similarity', fontsize=11)
ax1.set_title('Training Sample vs Real Input\nCosine Similarity (word: "Dubu")', fontsize=11, fontweight='bold')
ax1.legend(fontsize=9); ax1.yaxis.grid(True, alpha=0.4); ax1.set_axisbelow(True)

methods = ['Resolution\nfix','SYN\nMediaPipe','Pixel\nnorm','Dist.\ncorrect','Joint\norder(v6)','Delta\n(v7)']
results = [91.48, 0, 83.65, 91.48, 93.18, 93.69]
rc      = ['#D3D1C7','#F09595','#F09595','#D3D1C7','#1D9E75','#0F6E56']
b2 = ax2.bar(methods, results, color=rc, width=0.6, zorder=3)
for bar, r in zip(b2, results):
    label = f'{r:.2f}%' if r > 0 else 'FAIL\n(0%)'
    ypos  = bar.get_height()+0.5 if r > 0 else 3
    ax2.text(bar.get_x()+bar.get_width()/2, ypos, label,
             ha='center', va='bottom', fontsize=9, fontweight='bold')
ax2.set_ylim(0, 97); ax2.set_ylabel('Test Accuracy (%)', fontsize=11)
ax2.set_title('False Positive Fix Attempts\n(baseline: v5 91.48%)', fontsize=11, fontweight='bold')
ax2.legend(handles=[
    mpatches.Patch(color='#F09595', label='Failed'),
    mpatches.Patch(color='#D3D1C7', label='No effect'),
    mpatches.Patch(color='#1D9E75', label='Success'),
], fontsize=9)
ax2.yaxis.grid(True, alpha=0.4); ax2.set_axisbelow(True)
plt.suptitle('False Positive Analysis & Resolution Process', fontsize=14, fontweight='bold')
plt.tight_layout()
plt.savefig('portfolio_images/04_fp_analysis.png', dpi=150, bbox_inches='tight')
plt.close()
print("04 done")
print("\nAll images saved to portfolio_images/")