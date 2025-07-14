#!/usr/bin/env python3
import argparse
import re
import matplotlib.pyplot as plt

def parse_plugin_stride(file_path):
    """Parse plugin_stride file to extract stride type counts."""
    stride_counts = {}
    with open(file_path, 'r') as f:
        lines = f.readlines()
    recording = False
    for line in lines:
        raw = line.strip()
        # Start after the header
        if raw.startswith("=== Memory Access Stride Statistics"):
            recording = True
            continue
        if not recording:
            continue
        # Skip total and header lines
        if raw.startswith("Total memory accesses"):
            continue
        if raw.startswith("Stride Type"):
            continue
        # Parse lines like: Same Address       12345      67.89%
        m = re.match(r'^(.+?)\s+(\d+)\s+([\d\.]+)%$', raw)
        if m:
            stride = m.group(1).strip()
            count = int(m.group(2))
            stride_counts[stride] = count
    return stride_counts

def plot_stride_pie(stride_counts, output_file=None):
    labels = list(stride_counts.keys())
    sizes  = list(stride_counts.values())
    explode = [0.02] * len(sizes)

    fig, ax = plt.subplots()
    wedges, texts, autotexts = ax.pie(
        sizes,
        explode=explode,
        labels=None,
        autopct='%1.2f%%',
        pctdistance=0.75,
        labeldistance=1.1,
        startangle=90,
    )
    # Legend with label (count)
    total = sum(sizes)
    legend_labels = [
        f"{lab} ({cnt}, {cnt/total*100:.2f}%)"
        for lab, cnt in zip(labels, sizes)
    ]
    ax.legend(
        wedges,
        legend_labels,
        title="Stride Types",
        loc="center left",
        bbox_to_anchor=(1, 0, 0.3, 1)
    )
    ax.set(aspect="equal")
    plt.title("Memory Access Stride Distribution")
    plt.tight_layout()

    if output_file:
        plt.savefig(output_file, bbox_inches='tight')
    else:
        plt.show()

def main():
    parser = argparse.ArgumentParser(
        description="Plot pie chart of memory access stride statistics from plugin_stride"
    )
    parser.add_argument('file', help="Path to the plugin_stride file")
    parser.add_argument('-o', '--output', help="Output image file (e.g., stride_pie.png)")
    args = parser.parse_args()

    stride_counts = parse_plugin_stride(args.file)
    if not stride_counts:
        print("No stride data found in the file.")
        return
    plot_stride_pie(stride_counts, args.output)

if __name__ == "__main__":
    main()

