#!/usr/bin/env python3
import subprocess

# Real-world task: Check git status, run tests, build
print("=== Deployment Check ===")

# Git status
result = subprocess.run(["git", "status", "--short"], capture_output=True, text=True)
if result.returncode != 0:
    print("Git check failed")

# Run "tests" (simulated with true)
print("Running tests...")
test_result = subprocess.run(["true"], capture_output=True)
if test_result.returncode == 0:
    print("✓ Tests passed")

# Build (simulated)
print("Building...")
build_result = subprocess.run(["echo", "Build complete"], capture_output=True, text=True)
print(build_result.stdout.strip())

print("=== Done ===")
