#!/usr/bin/env python3
"""Check the Master pages and saturation settings through physical buttons."""

import argparse
from pathlib import Path
import tempfile

from desktop_perf_fx_check import run


def check(binary):
    with tempfile.TemporaryDirectory(prefix="st-master-") as directory:
        card = Path(directory)
        audit = card / "master.tsv"
        run(binary, card, f"""audit-frames {audit}
button Master
button 1
button 7
button 2
button 7
button 3
button Down
button 1
encoder 4
button 7
button 4
button 7
button 5
button 7
button 6
button 7
wait 10500
""")
        frames = [
            [(int(x), int(y), text) for line in frame.splitlines() if "\t" in line
             for x, y, text in [line.split("\t", 2)]]
            for frame in audit.read_text().split("frame\n")[1:]
        ]
        labels = ("Volume", "Delay", "Reverb", "Saturation", "Limiter", "EQ", "Bass Boost", "Space")
        master_toolbars = [[(x, text) for x, y, text in frame if y >= 424]
                           for frame in frames if any(text == "Master 1/3" for _, _, text in frame)]
        assert any(all((50 + i * 100, name) in toolbar for i, name in enumerate(labels))
                   for toolbar in master_toolbars)
        for toolbar in master_toolbars:
            if (350, "Saturation") in toolbar:
                assert [text for x, text in toolbar if x == 350] == ["Saturation"], toolbar
        for title in ("Master Delay", "Master Reverb", "Master Saturation", "Master Limiter", "Master EQ"):
            assert any(any(text == title for _, _, text in frame) for frame in frames), title
        master = next(frame for frame in frames if any(text == "20k" for _, _, text in frame))
        assert (300, 50, "Mix") in master
        assert (650, 50, "Out") in master
        ticks = [(x, y, text) for x, y, text in master
                 if text in ("50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k") and 390 < y < 409]
        assert {text for _, _, text in ticks} == {"50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"}
        assert len({y for _, y, _ in ticks}) == 1
        assert 100 <= min(x for x, _, _ in ticks) < 145
        assert 475 < max(x for x, _, _ in ticks) < 500

        reload_audit = card / "master-reload.tsv"
        run(binary, card, f"""audit-frames {reload_audit}
button Master
button 3
button 1
button Delete
button 0
button Delete
button 7
""")
        texts = reload_audit.read_text()
        for text in ("Master Saturation", "Saturate", "2.0 dB", "0.0 dB", "Soft Clip"):
            assert f"\t{text}\n" in texts, text
    print("Master buttons, saturation reload and reset, and spectrum footprint passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    check(parser.parse_args().binary.resolve())
