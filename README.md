# Glipt — Glue + Script

**A safe scripting language for orchestrating modern systems.**

[![CI](https://github.com/glipt-org/glipt/actions/workflows/ci.yml/badge.svg)](https://github.com/glipt-org/glipt/actions/workflows/ci.yml)

Shell scripts get the job done but are fragile — easy to inject, hard to read, terrible at structured data. Full programming languages are safe but verbose for automation work. Glipt lives in between: clean syntax, deny-by-default permissions, first-class process execution, and a bytecode VM that's measurably faster than CPython.

---

## Why Glipt?

| Feature | Shell | Python | Glipt |
|---------|-------|--------|-------|
| Readable syntax | ❌ | ✅ | ✅ |
| Deny-by-default permissions | ❌ | ❌ | ✅ |
| First-class process execution | ✅ | ⚠️ | ✅ |
| Built-in JSON | ❌ | ⚠️ | ✅ |
| Pattern matching | ❌ | ✅ (3.10+) | ✅ |
| Module imports | ❌ | ✅ | ✅ |
| Zero dependencies | ✅ | ❌ | ✅ |
| Single binary | ✅ | ❌ | ✅ |

---

## Performance

Glipt beats CPython on compute and orchestration workloads:

| Benchmark | Glipt | Python | Result |
|-----------|-------|--------|--------|
| Fibonacci(30) | 0.068s | 0.23s | **▲ 3.4x faster** |
| Loops (5M iterations) | 0.17s | 0.78s | **▲ 4.6x faster** |
| Closures (500k calls) | 0.042s | 0.20s | **▲ 4.8x faster** |
| Maps (50k inserts) | 0.065s | 0.10s | **▲ 1.6x faster** |

*Benchmarks run on a Ryzen 5000 series CPU, macOS, Glipt built with `-O2`.*

Speed comes from NaN-boxed values (8-byte `uint64_t`), computed-goto dispatch, direct stack pointer manipulation, and a global variable inline cache that skips hash table lookups on warm access.

---

## Installation

### Pre-built Binaries

Download the latest release from the [Releases page](../../releases):

| Platform | Binary |
|----------|--------|
| Linux x86_64 | `glipt-linux-x86_64` |
| macOS ARM64 | `glipt-macos-arm64` |
| Windows x86_64 | `glipt-windows-x86_64.exe` |

```bash
chmod +x glipt-*
./glipt version
```

### Build from Source

```bash
git clone <repository>
cd glipt
make
./glipt version
```

**Requirements**: C11 compiler (GCC 4.7+, Clang 3.1+), pthreads, math library. On Windows, use [MSYS2](https://www.msys2.org/) with `mingw-w64-x86_64-gcc`.

**Binary size**: ~100KB stripped, ~200KB debug build with symbols.

---

## Quick Start

```bash
./glipt                              # Start REPL
./glipt run script.glipt             # Run a script (deny-by-default)
./glipt run --allow-all script.glipt # Run with all permissions
./glipt check script.glipt           # Syntax check only
./glipt update                       # Check for updates
```

---

## Language Tour

### Variables

```glipt
name = "Alice"
age = 30
active = true
nothing = nil
```

### String Interpolation (f-strings)

```glipt
println(f"Hello {name}, you are {age} years old!")
println(f"{age * 2} in dog years")
println(f"Count: {len([1, 2, 3])}")
```

### Functions and Closures

```glipt
fn greet(name) {
    return f"Hello, {name}!"
}

fn counter(start) {
    n = start
    return fn() {
        n = n + 1
        return n
    }
}

c = counter(0)
print(c())  # 1
print(c())  # 2
```

### Control Flow

```glipt
if score >= 90 {
    grade = "A"
} else if score >= 80 {
    grade = "B"
} else {
    grade = "C"
}

while retries > 0 {
    result = exec("connect")
    if result["exitCode"] == 0 { break }
    retries = retries - 1
}

for item in ["a", "b", "c"] {
    print(item)
}
```

### Match Expressions

```glipt
status = match code {
    200 -> "ok"
    404 -> "not found"
    500 -> "server error"
    _   -> "unknown"
}

# Block bodies work too
match env("APP_ENV") {
    "production" -> {
        log_level = "warn"
        debug = false
    }
    "development" -> {
        log_level = "debug"
        debug = true
    }
}
```

### Data Structures

```glipt
# Lists
nums = [1, 2, 3, 4, 5]
append(nums, 6)
doubled = map_fn(nums, fn(x) { return x * 2 })
evens   = filter(nums, fn(x) { return x % 2 == 0 })
total   = reduce(nums, fn(a, b) { return a + b }, 0)

# Maps
person = {"name": "Alice", "age": 30}
print(person["name"])   # Alice
print(person.age)       # 30 — dot notation works too
```

### Process Execution

```glipt
allow exec "git*"

result = exec("git log --oneline -n 5")
print(result["output"])    # trimmed stdout
print(result["exitCode"])  # 0

assert(result["exitCode"] == 0, "git failed")
```

### Parallel Execution

```glipt
allow exec "curl*"

results = parallel_exec([
    "curl -s https://api1.example.com/data",
    "curl -s https://api2.example.com/data",
    "curl -s https://api3.example.com/data"
])

for r in results {
    print(r["output"])
}
```

### HTTP / HTTPS

```glipt
allow net "*"

response = net.get("https://api.github.com/repos/glipt-org/glipt/releases/latest")
print(response["status"])  # 200

data = parse_json(response["body"])
print(data["tag_name"])
```

### JSON

```glipt
allow read "config/*"

config = parse_json(read("config/app.json"))
print(config["database"]["host"])

payload = {"name": "Alice", "active": true}
write("output.json", to_json(payload))
```

### Error Handling

```glipt
on failure {
    print(f"Failed: {error["message"]} (type: {error["type"]}, line: {error["line"]})")
    exec("rollback.sh")
    exit(1)
}

# Errors in anything below are caught
result = exec("risky-command")
data = parse_json(result["output"])
```

### Import System

```glipt
import "lib/utils"           # available as 'utils'
import "lib/http" as http    # available as 'http'

print(utils.greet("Alice"))
response = http.get("https://example.com")
```

Module files are plain Glipt scripts — top-level functions and variables become the module's exports.

### Permissions

```glipt
allow exec "git*"            # only git commands
allow exec "docker*"         # only docker commands
allow read "config/*"        # only the config directory
allow write "logs/*"         # only the logs directory
allow env "DATABASE_URL"     # only this env var
allow net "api.example.com"  # only this host

# Without permission, operations fail with a clear error
exec("rm -rf /")  # Permission denied: exec "rm"
```

---

## Standard Library Modules

All modules are available as global maps — no import statement needed.

### `math`

```glipt
print(math.PI)                  # 3.14159...
print(math.sqrt(144))           # 12
print(math.pow(2, 10))          # 1024
print(math.round(3.7))          # 4
print(math.min(1, 5, 3, 2))    # 1  (variadic)
print(math.max(1, 5, 3, 2))    # 5  (variadic)
print(math.log2(1024))          # 10
print(math.trunc(3.9))          # 3
print(math.sign(-5))            # -1
print(math.sin(math.PI / 2))    # 1
print(math.rand())              # random float 0..1
print(math.rand_int(1, 100))    # random int 1..100
```

Functions: `floor`, `ceil`, `round`, `abs`, `sqrt`, `pow`, `log`, `log10`, `log2`, `exp`, `min`, `max`, `sign`, `trunc`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `rand`, `rand_int`

`min` and `max` accept any number of arguments: `math.min(a, b, c, ...)`.

Constants: `PI`, `E`, `INF`, `NAN`

### `re` (Regex)

Uses POSIX Extended Regular Expressions.

```glipt
# Full-string match
re.match("^[0-9]+$", "12345")   # true

# First match with position info
m = re.search("[0-9]+", "abc123def")
print(m["matched"])  # "123"
print(m["start"])    # 3

# Capture groups — returned as m["groups"] list
m = re.search("([a-z]+)=([0-9]+)", "key=42")
print(m["groups"][0])  # "key"
print(m["groups"][1])  # "42"

# All matches
nums = re.find_all("[0-9]+", "a1b22c333")
print(nums)  # ["1", "22", "333"]

# Replace
result = re.replace("[0-9]+", "abc123", "X")
print(result)  # "abcX"

# Split
parts = re.split("[,;]", "a,b;c")
print(parts)  # ["a", "b", "c"]
```

Note: POSIX ERE doesn't support `\s`, `\d` etc. Use `[[:space:]]`, `[0-9]` instead.

### `fs` (File System)

```glipt
allow read "*"
allow write "*"

fs.exists("path/to/file")
fs.isdir("some/dir")
fs.list(".")                     # returns list of filenames
fs.mkdir("new/dir")
fs.copy("src.txt", "dst.txt")
fs.move("old.txt", "new.txt")
fs.remove("file.txt")
fs.stat("file.txt")              # {size, modified, isdir, isfile}
fs.join("path", "to", "file")   # OS-appropriate path join
fs.basename("path/to/file.txt") # "file.txt"
fs.dirname("path/to/file.txt")  # "path/to"
fs.extname("file.txt")          # ".txt"
```

### `net` (HTTP/HTTPS)

```glipt
allow net "*"

net.get("https://example.com")
net.post("https://api.example.com/data", to_json({"key": "value"}))
net.put("https://api.example.com/item/1", body)
net.delete("https://api.example.com/item/1")
net.resolve("example.com")   # DNS lookup, returns list of IPs
```

Returns `{status, body}`. HTTPS uses the system `curl` transparently.

### `proc` (Process Management)

```glipt
allow exec "*"

proc.exec("ls -la")     # {output, code}
proc.pid()              # current PID
proc.running(pid)       # bool
proc.kill(pid, 15)      # send signal
proc.sleep(1.5)         # sleep in seconds
```

### `sys` (System Info)

```glipt
sys.platform()   # "linux", "darwin", "windows"
sys.arch()       # "amd64", "arm64"
sys.cpu_count()  # number of logical CPUs
sys.hostname()   # machine hostname
sys.username()   # current user
sys.cwd()        # current working directory
sys.pid()        # current process PID
sys.time()       # Unix timestamp
sys.args()       # script arguments as list
```

### `bit` (Bitwise Operations)

Integer bitwise operations via a module (all values are treated as 32-bit unsigned integers):

```glipt
print(bit.and(0b1100, 0b1010))    # 8   (0b1000)
print(bit.or(0b1100, 0b1010))     # 14  (0b1110)
print(bit.xor(0b1100, 0b1010))    # 6   (0b0110)
print(bit.not(0))                 # 4294967295
print(bit.lshift(1, 4))           # 16
print(bit.rshift(256, 4))         # 16
```

Functions: `and`, `or`, `xor`, `not`, `lshift`, `rshift`

---

## Built-in Functions

**I/O**: `print(...)`, `println(...)`, `input(prompt?)`, `read(path)`, `write(path, content)`

**Strings**: `split(str, delim)`, `join(list, sep)`, `trim(str)`, `replace(str, old, new)`, `upper(str)`, `lower(str)`, `starts_with(str, prefix)`, `ends_with(str, suffix)`, `format(fmt, ...)`, `substr(str, start, end?)`, `index_of(str, sub)`, `repeat(str, n)`, `reverse(str)`, `lstrip(str)`, `rstrip(str)`, `char_at(str, i)`, `pad_start(str, len, fill?)`, `pad_end(str, len, fill?)`, `count(str, sub)`, `capitalize(str)`

**Type checks**: `is_number(str)`, `is_alpha(str)`

**Collections**: `len(x)`, `append(list, item)`, `pop(list)`, `sort(list, fn?)`, `keys(map)`, `values(map)`, `contains(x, item)`, `range(start, stop, step?)`, `map_fn(list, fn)`, `filter(list, fn)`, `reduce(list, fn, init?)`, `slice(list, start, end?)`, `insert(list, i, item)`, `find(list, item)`, `remove(list, i)`, `sum(list)`, `unique(list)`, `reverse(list)`

**Types**: `type(x)`, `str(x)`, `num(x)`, `bool(x)`

**Process & System**: `exec(cmd)`, `parallel_exec([cmd, ...])`, `env(name, default?)`, `sleep(seconds)`, `exit(code?)`

**Data**: `parse_json(str)`, `to_json(value)`

**Utilities**: `clock()`, `assert(cond, msg?)`, `debug(...)`

---

## Real-World Examples

### Deploy Script

```glipt
allow exec "git*"
allow exec "docker*"
allow read "config/*"
allow write "logs/*"

config  = parse_json(read("config/deploy.json"))
app     = config["app"]
version = config["version"]

on failure {
    println(f"Deploy failed: {error["message"]}")
    exec(f"docker rollback {app}")
    exit(1)
}

exec("git pull origin main")

parallel_exec([
    f"docker build -t {app}:{version} .",
    f"docker push {app}:{version}"
])

exec(f"docker service update --image {app}:{version} {app}")
sleep(5)

health = exec("curl -sf http://localhost:8080/health")
assert(health["exitCode"] == 0, "Health check failed")

println(f"Deployed {app}:{version}")
write(f"logs/deploy-{version}.log", f"deployed at {str(clock())}")
```

### Data Pipeline

```glipt
allow net "api.example.com"
allow write "output/*"
allow env "API_KEY"

key = env("API_KEY")

results = parallel_exec([
    f"curl -H 'Authorization: {key}' https://api.example.com/users",
    f"curl -H 'Authorization: {key}' https://api.example.com/orders",
])

users  = parse_json(results[0]["output"])
orders = parse_json(results[1]["output"])

enriched = map_fn(orders, fn(order) {
    user = filter(users, fn(u) { return u["id"] == order["user_id"] })[0]
    return {"order_id": order["id"], "user": user["name"], "total": order["total"]}
})

write("output/enriched.json", to_json(enriched))
println(f"Processed {len(enriched)} orders")
```

### CI Pipeline

```glipt
allow exec "npm*"
allow exec "git*"
allow exec "docker*"

branch = trim(exec("git rev-parse --abbrev-ref HEAD")["output"])
println(f"Building: {branch}")

on failure {
    println(f"Build failed: {error["message"]}")
    exit(1)
}

exec("npm install")
exec("npm test")
exec("npm run build")

if branch == "main" {
    sha = trim(exec("git rev-parse --short HEAD")["output"])
    exec(f"docker build -t myapp:{sha} .")
    exec(f"docker push myapp:{sha}")
    exec(f"kubectl set image deployment/myapp myapp=myapp:{sha}")
    println(f"Deployed {sha}")
}
```

---

## CLI Reference

```
glipt                          Start REPL
glipt repl                     Start REPL
glipt run <script> [args...]   Run script (deny-by-default permissions)
glipt run --allow-all <script> Run with all permissions
glipt check <script>           Syntax check only
glipt disasm <script>          Show bytecode disassembly
glipt ast <script>             Show AST (debug)
glipt tokens <script>          Show token stream (debug)
glipt update                   Check for updates
glipt version                  Show version
glipt help                     Show help
```

Script arguments are available via `sys.args()`:

```bash
glipt run --allow-all script.glipt foo bar
```
```glipt
args = sys.args()  # ["foo", "bar"]
```

---

## REPL

```bash
$ ./glipt
Glipt 0.1.1 REPL (type 'exit' to quit)
>>> name = "World"
>>> println(f"Hello {name}!")
Hello World!
>>> fn double(n) { return n * 2 }
>>> double(21)
42
>>> exit
```

The REPL has all permissions enabled and state persists between lines.

---

## Build System

```bash
make              # Release build (-O2, strict warnings)
make debug        # Debug build (DEBUG_TRACE enabled, stress GC)
make test         # Build and run all tests
make clean        # Remove build artifacts
```

---

## Testing

```bash
./run_tests.sh     # Run all 12 test suites
make test          # Build first, then run
```

**Test suites (12 total):**
- `milestone1.glipt` — Basic types, arithmetic, strings, lists, maps
- `milestone2.glipt` — Functions, recursion, closures, loops, if/else
- `milestone3.glipt` — Process execution, JSON, files, env, error handling
- `full_test.glipt` — All features (37 assertions)
- `exec_test.glipt` — Process execution edge cases
- `parallel_test.glipt` — Concurrent execution
- `stdlib_test.glipt` — fs, proc, net, sys modules
- `math_test.glipt` — math module
- `regex_test.glipt` — re module (including capture groups)
- `match_test.glipt` — Match expressions
- `import_test.glipt` — Import system
- `phase3_test.glipt` — String/list builtins, bit module, range values, sort comparators, type checks

---

## CI/CD

Every push and pull request builds and tests on Linux, macOS, and Windows via GitHub Actions.

**Cutting a release**: push a `v*` tag and GitHub Actions builds all three platform binaries and publishes a GitHub Release automatically:

```bash
git tag v1.0.0
git push origin v1.0.0
```

---

## Contributing

- C11 standard, 4-space indentation, no external dependencies
- All builds must pass with `-Wall -Wextra -Werror`
- Run `make test` before submitting changes

---

## License

MIT License — see LICENSE file
