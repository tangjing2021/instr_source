#!/usr/bin/env python3
import argparse
import re
import matplotlib.pyplot as plt

def parse_plugin_time(file_path):
    instruction_times = {}
    total_time_ms = None
    with open(file_path, 'r') as f:
        lines = f.readlines()
    recording = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("=== Instruction Class Timing"):
            recording = True
            continue
        if not recording:
            # Also capture total elapsed time before timing section
            m_total = re.match(r'^>>> Total Elapsed Time:\s*([\d\.]+)\s*ms', stripped)
            if m_total:
                total_time_ms = float(m_total.group(1))
            continue
        # Inside timing section, stop when seeing total elapsed (in case repeated)
        if stripped.startswith(">>> Total Elapsed Time"):
            m_total = re.match(r'^>>> Total Elapsed Time:\s*([\d\.]+)\s*ms', stripped)
            if m_total:
                total_time_ms = float(m_total.group(1))
            break
        if stripped.startswith("Class"):
            continue
        if ':' in stripped:
            parts = re.split(r'\s*:\s*', stripped)
            if len(parts) == 2:
                instr, time_str = parts
                try:
                    instruction_times[instr.strip()] = float(time_str.split()[0])
                except ValueError:
                    pass
    return instruction_times, total_time_ms

def plot_pie(times, total_time_ms, output_file=None):
    labels = list(times.keys())
    sizes  = list(times.values())
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

    # Build legend labels with absolute ms and percentage
    total = sum(sizes)
    legend_labels = [
        f"{lab} ({val:.2f} ms, {val/total*100:.1f}%)"
        for lab, val in zip(labels, sizes)
    ]
    ax.legend(
        wedges,
        legend_labels,
        title="Instruction Classes",
        loc="center left",
        bbox_to_anchor=(1, 0, 0.3, 1)
    )

    ax.set(aspect="equal")
    title = "Instruction Class Time Distribution"
    if total_time_ms is not None:
        title += f"\nTotal Elapsed Time: {total_time_ms:.2f} ms"
    plt.title(title)
    plt.tight_layout()

    if output_file:
        plt.savefig(output_file, bbox_inches='tight')
    else:
        plt.show()

def main():
    parser = argparse.ArgumentParser(
        description="Plot pie chart from plugin_time with total elapsed time"
    )
    parser.add_argument('file', help="plugin_time 文件路径")
    parser.add_argument('-o', '--output', help="输出图片文件名，如 chart.png")
    args = parser.parse_args()

    times, total_time_ms = parse_plugin_time(args.file)
    if not times:
        print("未找到任何时间数据。")
        return

    plot_pie(times, total_time_ms, args.output)

if __name__ == "__main__":
    main()


