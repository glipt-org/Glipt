sum = 0
i = 0
while i < 5000000:
    sum = sum + i * 2 - i
    i = i + 1
print(f"Sum: {sum}")
