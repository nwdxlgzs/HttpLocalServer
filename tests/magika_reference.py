"""Compare every CPU kernel against the pinned official ONNX graph.

Python numpy/onnxruntime are development-only reference dependencies.
"""
import argparse
import json
import pathlib
import subprocess
import tempfile

import numpy as np
import onnxruntime as ort

ROOT = pathlib.Path(__file__).resolve().parents[1]


def features(data):
    beginning = data[:4096].lstrip(b" \t\n\r\v\f")[:1024]
    ending = data[-4096:].rstrip(b" \t\n\r\v\f")[-1024:]
    result = np.full((1, 2048), 256, dtype=np.int32)
    result[0, :len(beginning)] = np.frombuffer(beginning, np.uint8)
    result[0, 2048-len(ending):] = np.frombuffer(ending, np.uint8)
    return result


def run(probe, output=None):
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    reference = ort.InferenceSession(str(ROOT / "third_party/magika/standard_v3_3.onnx"), options, providers=["CPUExecutionProvider"])
    random = np.random.default_rng(20261110)
    examples = {
        "python": b"#!/usr/bin/env python3\nimport pathlib\ndef square(value):\n    return value * value\nprint(square(9))\n" * 30,
        "cpp": b"#include <iostream>\n#include <vector>\nint main(){std::vector<int> values{1,2,3};for(auto v:values)std::cout<<v<<std::endl;}\n" * 30,
        "json": json.dumps({"name": "example", "items": list(range(100)), "enabled": True}).encode(),
        "shell": b"#!/bin/sh\nset -eu\nfor f in *.txt; do printf '%s\\n' \"$f\"; done\n" * 30,
        "csv": b"name,age,score\nAlice,20,91.5\nBob,22,87.8\n" * 80,
        "html": b"<!doctype html><html><head><title>Page</title></head><body><h1>hello</h1></body></html>" * 20,
        "text": "A simple document about daily work.\n这里是中文测试，包含段落和标点。\n".encode() * 90,
        "binary": random.integers(0, 256, 20000, dtype=np.uint8).tobytes(),
        "whitespace": b" \t\r\n\v\f" * 900,
        "short": b"hello!",
    }
    cases = [(name, features(data)) for name, data in examples.items()]
    cases += [("tokens-" + str(i), random.integers(0, 257, (1, 2048), dtype=np.int32)) for i in range(3)]
    kernels = subprocess.check_output([str(probe), "--list"], text=True).splitlines()
    report = {"reference": "ONNX Runtime CPU", "cases": len(cases), "kernels": {}}
    with tempfile.TemporaryDirectory(prefix="hls-magika-reference-") as temporary:
        paths = []
        expected = []
        for name, data in cases:
            path = pathlib.Path(temporary) / (name + ".i32")
            path.write_bytes(data.astype("<i4").tobytes())
            paths.append(path)
            expected.append(reference.run(None, {"bytes": data})[0][0])
        for kernel in kernels + ["auto"]:
            rows = subprocess.check_output([str(probe), "--kernel", kernel, *map(str, paths)], text=True).splitlines()
            assert len(rows) == len(cases)
            largest = 0.0
            labels = {}
            elapsed = []
            for (name, _), scores, line in zip(cases, expected, rows):
                result = json.loads(line)
                actual = np.asarray(result["scores"], np.float32)
                difference = float(np.max(np.abs(actual - scores)))
                largest = max(largest, difference)
                assert actual.argmax() == scores.argmax(), (kernel, name, "classification mismatch")
                assert difference < 0.0003, (kernel, name, difference)
                labels[name] = result["label"]
                elapsed.append(result["milliseconds"])
            report["kernels"][kernel] = {"selected": result["kernel"], "maximumProbabilityError": largest, "medianMilliseconds": float(np.median(elapsed)), "labels": labels}
            print(kernel, report["kernels"][kernel]["selected"], "maximum error", largest, "median ms", float(np.median(elapsed)), flush=True)
    if output:
        output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("probe", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    run(args.probe.resolve(), args.output)
