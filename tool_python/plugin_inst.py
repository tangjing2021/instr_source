#!/usr/bin/env python3
import argparse
import re
import numpy as np
import matplotlib.pyplot as plt

def parse_plugin_inst(file_path):
    static_counts = {}
    dynamic_counts = {}
    with open(file_path, 'r') as file:
        lines = file.readlines()

    recording = False
    for line in lines:
        # keep leading spaces for accurate matching
        raw = line.rstrip('\n')
        if raw.startswith("=== Global Instruction Counts"):
            recording = True
            continue
        if recording and raw.startswith(">>> Global Dynamic Instructions Total"):
            break
        if recording and ':' in raw:
            # match everything before the colon as the instruction name
            match = re.match(r'^(.+?)\s*:\s*static=\s*(\d+)\s*dynamic=\s*(\d+)', raw)
            if match:
                instr = match.group(1).strip()
                static = int(match.group(2))
                dynamic = int(match.group(3))
                static_counts[instr] = static
                dynamic_counts[instr] = dynamic

    return static_counts, dynamic_counts

def plot_bar(static_counts, dynamic_counts, use_log=False, output_file=None):
    instrs = sorted(set(static_counts.keys()) | set(dynamic_counts.keys()))
    static_vals = [static_counts.get(i, 0) for i in instrs]
    dynamic_vals = [dynamic_counts.get(i, 0) for i in instrs]

    x = np.arange(len(instrs))
    width = 0.4

    plt.figure(figsize=(max(10, len(instrs)*0.6), 6))
    plt.bar(x - width/2, static_vals, width, label='Static')
    plt.bar(x + width/2, dynamic_vals, width, label='Dynamic')

    plt.xlabel('Instruction Type')
    plt.ylabel('Count')
    plt.title('Static vs Dynamic Instruction Counts')
    plt.xticks(x, instrs, rotation=45, ha='right')
    if use_log:
        plt.yscale('log')
    plt.legend()
    plt.tight_layout()

    if output_file:
        plt.savefig(output_file, bbox_inches='tight')
    else:
        plt.show()

def main():
    parser = argparse.ArgumentParser(description="Plot static vs dynamic instruction counts")
    parser.add_argument('file', help="Path to the plugin_inst file")
    parser.add_argument('--log', action='store_true', help="Use logarithmic scale for Y axis")
    parser.add_argument('-o', '--output', help="Output image file (e.g., chart.png)")
    args = parser.parse_args()

    static_counts, dynamic_counts = parse_plugin_inst(args.file)
    if not static_counts and not dynamic_counts:
        print("No instruction count data found in the file.")
        return

    plot_bar(static_counts, dynamic_counts, args.log, args.output)

if __name__ == "__main__":
    main()


