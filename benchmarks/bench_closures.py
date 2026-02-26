def make_counter():
    count = [0]
    def increment():
        count[0] = count[0] + 1
        return count[0]
    return increment

def run_counters(n):
    c1 = make_counter()
    c2 = make_counter()
    c3 = make_counter()
    i = 0
    while i < n:
        c1()
        c2()
        c3()
        i = i + 1
    return c1() + c2() + c3()

result = run_counters(500000)
print(f"Counter total: {result}")
