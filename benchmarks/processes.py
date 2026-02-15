#!/usr/bin/env python3
import subprocess

# Run 100 simple commands
for i in range(100):
    subprocess.run(["echo", "test"], capture_output=True)
print("Done: 100 processes")
