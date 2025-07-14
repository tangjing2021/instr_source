#!/usr/bin/env python3
import argparse
import re
import matplotlib.pyplot as plt
import numpy as np

def parse_plugin_vaddr(file_path):
    addresses = []
    reads = []
    writes = []
    totals = []
    with open(file_path, 'r') as f:
        lines = f.readlines()
    
    recording = False
    for line in lines:
        line = line.strip()
        if line.startswith("=== Top"):
            recording = True
            continue
        # skip header line
        if recording and line.startswith("Address"):
            continue
        # parse data lines: addr reads writes total
        if recording and line.startswith("0x"):
            m = re.match(r'^(0x[0-9a-fA-F]+)\s+(\d+)\s+(\d+)\s+(\d+)', line)
            if m:
                addr, rd, wr, tot = m.groups()
                addresses.append(addr)
                reads.append(int(rd))
                writes.append(int(wr))
                totals.append(int(tot))
    return addresses, reads, writes, totals

def plot_hotspots(addresses, reads, writes, totals, output_file=None):
    x = np.arange(len(addresses))
    width = 0.25

    plt.figure(figsize=(max(8, len(addresses)*0.6), 6))
    plt.bar(x - width, reads, width, label='Reads')
    plt.bar(x, writes, width, label='Writes')
    plt.bar(x + width, totals, width, label='Total')

    plt.xlabel('Hot Address')
    plt.ylabel('Count')
    plt.title('Top Hot Memory Addresses Read/Write Counts')
    plt.xticks(x, addresses, rotation=45, ha='right')
    plt.legend()
    plt.tight_layout()

    if output_file:
        plt.savefig(output_file, bbox_inches='tight')
    else:
        plt.show()

def main():
    parser = argparse.ArgumentParser(description="Plot hot address read/write counts")
    parser.add_argument('file', help="Path to the plugin_vaddr file")
    parser.add_argument('-o', '--output', help="Output image file (e.g., hotspots.png)")
    args = parser.parse_args()

    addrs, rd, wr, tot = parse_plugin_vaddr(args.file)
    if not addrs:
        print("No hotspot address data found in the file.")
        return

    plot_hotspots(addrs, rd, wr, tot, args.output)

if __name__ == "__main__":
    main()


