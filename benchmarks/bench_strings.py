result = ""
i = 0
while i < 5000:
    result = result + str(i) + ","
    i = i + 1
print(f"Length: {len(result)}")

count = 0
i = 0
while i < len(result):
    c = result[i]
    if c != ",":
        count = count + 1
    i = i + 1
print(f"Digit chars: {count}")
