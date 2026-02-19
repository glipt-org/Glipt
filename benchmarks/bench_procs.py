import subprocess
for i in range(100):
    subprocess.run(["echo", "test"], capture_output=True)
print("Done: 100 processes")
