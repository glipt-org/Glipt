#!/usr/bin/env python3
# Fibonacci benchmark
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

result = fib(30)
print(f"Fibonacci(30) = {result}")
