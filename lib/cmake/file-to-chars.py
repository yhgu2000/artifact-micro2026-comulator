#!/bin/env python3

import argparse


def file_to_chars(input, output):
    with open(input, "rb") as fi, open(output, "w") as fo:
        while sz := fi.read(16):
            fo.write(",".join(f"0x{b:02x}" for b in sz) + ",\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert a file to C chars")
    parser.add_argument("input", type=str, help="Input file")
    parser.add_argument("output", type=str, help="Output file", nargs="?")
    args = parser.parse_args()
    if not args.output:
        args.output = args.input + ".chars"
    file_to_chars(args.input, args.output)
