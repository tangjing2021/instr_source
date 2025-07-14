#!/usr/bin/env python3
import argparse
import re
import matplotlib.pyplot as plt
import numpy as np

def parse_dynamic_counts(file_path):
    """Parse a plugin_inst file and return a dict of instruction -> dynamic count."""
    dynamic_counts = {}
    with open(file_path, 'r') as f:
        lines = f.readlines()
    recording = False
    for line in lines:
        raw = line.strip()
        if raw.startswith("=== Global Instruction Counts"):
            recording = True
            continue
        if recording and raw.startswith(">>> Global Dynamic Instructions Total"):
            break
        if recording and ':' in raw:
            m = re.match(r'^(.+?)\s*:\s*static=\s*\d+\s*dynamic=\s*(\d+)', raw)
            if m:
                instr = m.group(1).strip()
                dyn = int(m.group(2))
                dynamic_counts[instr] = dyn
    return dynamic_counts

def plot_comparison(files, labels, output_file=None):
    # Parse each file
    data = [parse_dynamic_counts(f) for f in files]
    # Collect all instruction types
    instrs = sorted(set().union(*[d.keys() for d in data]))
    x = np.arange(len(instrs))
    
    plt.figure(figsize=(max(10, len(instrs)*0.5), 6))
    for dyn_counts, label in zip(data, labels):
        y = [dyn_counts.get(instr, 0) for instr in instrs]
        plt.plot(x, y, marker='o', label=label)
    
    plt.xlabel('Instruction Type')
    plt.ylabel('Dynamic Count')
    plt.title('Dynamic Instruction Counts Comparison')
    plt.xticks(x, instrs, rotation=45, ha='right')
    plt.legend()
    plt.tight_layout()
    
    if output_file:
        plt.savefig(output_file, bbox_inches='tight')
    else:
        plt.show()

def main():
    parser = argparse.ArgumentParser(
        description="Compare dynamic instruction counts from three plugin_inst files"
    )
    parser.add_argument('simsmall', help="Path to simsmall plugin_inst file")
    parser.add_argument('simmedium', help="Path to simmedium plugin_inst file")
    parser.add_argument('simlarge', help="Path to simlarge plugin_inst file")
    parser.add_argument('-o', '--output', help="Output image file (e.g., compare.png)")
    args = parser.parse_args()
    
    files = [args.simsmall, args.simmedium, args.simlarge]
    labels = ['simsmall', 'simmedium', 'simlarge']
    plot_comparison(files, labels, args.output)

if __name__ == "__main__":
    main()

