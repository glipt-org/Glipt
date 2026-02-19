# Glipt - Glue + Script

**A Process Orchestration Language**

A safe, readable scripting language for orchestrating modern systems.

[![CI](https://github.com/glipt-org/glipt/actions/workflows/ci.yml/badge.svg)](https://github.com/glipt-org/glipt/actions/workflows/ci.yml)

## Why Glipt?

Shell scripts are powerful but dangerous (easy to inject commands, hard to read). Full programming languages are safe but verbose. Glipt gives you the best of both worlds:

- **Safe by default** - Explicit permissions for dangerous operations (deny-by-default)
- **Easy to read** - Clean syntax inspired by Go and Python
- **Process-aware** - First-class support for running commands and handling output
- **Structured data** - Built-in JSON parsing and serialization
- **Error handling** - Catch and handle failures gracefully with `on failure`
- **Parallel execution** - Run multiple commands concurrently
- **Zero dependencies** - Single binary, pure C11

## Performance

Glipt beats CPython on compute and orchestration workloads:

| Benchmark | Glipt | Python | Result |
|-----------|-------|--------|--------|
| Fibonacci(30) | 0.068s | 0.23s | **▲ 3.4x faster** |
| Loops (5M iterations) | 0.17s | 0.78s | **▲ 4.6x faster** |
| Closures (500k calls) | 0.042s | 0.20s | **▲ 4.8x faster** |
| Maps (50k inserts) | 0.065s | 0.10s | **▲ 1.6x faster** |

*Benchmarks run on a MacOS 26.2 Tahoe Hackintosh with a Ryzen 5000 Series CPU. Glipt built with `-O2`.*

Glipt achieves this through NaN-boxed values (8-byte `uint64_t`), computed-goto dispatch, direct stack pointer manipulation, and a global variable inline cache that skips hash table lookups after first access.

## Installation

### Pre-built Binaries

Download the latest release binary for your platform from the [Releases page](../../releases):

| Platform | Binary |
|----------|--------|
| Linux x86_64 | `glipt-linux-x86_64` |
| macOS ARM64 | `glipt-macos-arm64` |
| Windows x86_64 | `glipt-windows-x86_64.exe` |

```bash
# Linux / macOS
chmod +x glipt-*
./glipt-linux-x86_64 version
```

### Build from Source

```bash
git clone <repository>
cd glipt
make
./glipt version
```

**Requirements**: C11 compiler (GCC 4.7+, Clang 3.1+), pthreads, math library. On Windows, build via [MSYS2](https://www.msys2.org/) with `mingw-w64-x86_64-gcc`.

**Binary size**: ~100KB stripped release, ~200KB debug build with symbols.

## Quick Start

```bash
# Run the REPL
./glipt

# Run a script (deny-by-default permissions)
./glipt run script.glipt

# Run with all permissions (development mode)
./glipt run --allow-all script.glipt

# Check syntax without running
./glipt check script.glipt
```

## Real-World Use Cases

### 1. DevOps Automation

**Scenario**: Deploy a web application with health checks and rollback

```glipt
allow exec "git*"
allow exec "docker*"
allow exec "curl*"
allow read "config/*"
allow write "logs/*"

# Configuration
config = parse_json(read("config/deploy.json"))
app_name = config["app"]
version = config["version"]

# Error handler - rollback on failure
on failure {
    print("Deployment failed: " + error["message"])
    print("Rolling back...")
    exec("docker rollback " + app_name)
    exit(1)
}

# Pull latest code
result = exec("git pull origin main")
assert(result["exitCode"] == 0, "Git pull failed")

# Build and deploy in parallel
build_tasks = [
    "docker build -t " + app_name + ":" + version + " .",
    "docker push " + app_name + ":" + version
]
results = parallel_exec(build_tasks)

# Deploy
exec("docker service update --image " + app_name + ":" + version + " " + app_name)

# Health check
sleep(5)
health = exec("curl -f http://localhost:8080/health")
assert(health["exitCode"] == 0, "Health check failed")

print("✅ Deployment successful!")
write("logs/deploy-" + version + ".log", "Deployed at: " + str(clock()))
```

### 2. Data Pipeline

**Scenario**: ETL pipeline that fetches data from APIs and processes it

```glipt
allow exec "curl*"
allow read "data/*"
allow write "output/*"
allow env "API_KEY"

# Fetch data from multiple APIs in parallel
api_key = env("API_KEY")
apis = [
    "curl -H 'Authorization: " + api_key + "' https://api.example.com/users",
    "curl -H 'Authorization: " + api_key + "' https://api.example.com/orders",
    "curl -H 'Authorization: " + api_key + "' https://api.example.com/products"
]

on failure {
    print("API call failed: " + error["message"])
    exit(1)
}

results = parallel_exec(apis)

# Parse JSON responses
users = parse_json(results[0]["output"])
orders = parse_json(results[1]["output"])
products = parse_json(results[2]["output"])

# Transform data
enriched_orders = []
for order in orders {
    user = filter(users, fn(u) { return u["id"] == order["user_id"] })[0]
    product = filter(products, fn(p) { return p["id"] == order["product_id"] })[0]

    enriched = {
        "order_id": order["id"],
        "user_name": user["name"],
        "product_name": product["name"],
        "total": order["quantity"] * product["price"]
    }
    append(enriched_orders, enriched)
}

write("output/enriched_orders.json", to_json(enriched_orders))
print(format("Processed {} orders", len(enriched_orders)))
```

### 3. System Monitoring

**Scenario**: Monitor system health and send alerts

```glipt
allow exec "df*"
allow exec "ps*"
allow exec "curl*"
allow env "SLACK_WEBHOOK"

# Check disk space
disk = exec("df -h / | tail -n 1 | awk '{print $5}'")
disk_usage = num(replace(disk["output"], "%", ""))

if disk_usage > 80 {
    message = format("⚠️  Disk usage is {}%", disk_usage)
    webhook = env("SLACK_WEBHOOK")
    payload = to_json({"text": message})
    exec("curl -X POST -H 'Content-Type: application/json' -d '" + payload + "' " + webhook)
}

# Check for zombie processes
ps_output = exec("ps aux | grep defunct | wc -l")
zombies = num(ps_output["output"])

if zombies > 5 {
    print("⚠️  Found " + str(zombies) + " zombie processes")
}

print("✅ System health check complete")
```

### 4. Backup Script

**Scenario**: Backup databases and files with error handling

```glipt
allow exec "pg_dump*"
allow exec "tar*"
allow exec "aws*"
allow read "/var/lib/*"
allow write "/backups/*"
allow env "AWS_*"

timestamp = str(clock())
backup_dir = "/backups/" + timestamp

on failure {
    print("Backup failed: " + error["message"])
    exec("rm -rf " + backup_dir)
    exit(1)
}

exec("mkdir -p " + backup_dir)

backup_tasks = [
    "pg_dump mydb > " + backup_dir + "/db.sql",
    "tar czf " + backup_dir + "/files.tar.gz /var/lib/myapp"
]
results = parallel_exec(backup_tasks)

for result in results {
    assert(result["exitCode"] == 0, "Backup task failed")
}

exec("aws s3 cp " + backup_dir + " s3://my-backups/" + timestamp + "/ --recursive")
print("✅ Backup complete: " + backup_dir)
```

### 5. CI/CD Pipeline

**Scenario**: Test, build, and deploy application

```glipt
allow exec "npm*"
allow exec "git*"
allow exec "docker*"
allow read "*"
allow write "dist/*"

branch_result = exec("git rev-parse --abbrev-ref HEAD")
branch = trim(branch_result["output"])
print("Building branch: " + branch)

on failure {
    print("❌ Build failed: " + error["message"])
    exit(1)
}

print("📦 Installing dependencies...")
exec("npm install")

print("🧪 Running tests...")
test_result = exec("npm test")
assert(test_result["exitCode"] == 0, "Tests failed")

print("🔨 Building...")
exec("npm run build")

if branch == "main" {
    print("🚀 Deploying to production...")
    version = trim(exec("git rev-parse --short HEAD")["output"])
    image = "myapp:" + version
    exec("docker build -t " + image + " .")
    exec("docker push " + image)
    exec("kubectl set image deployment/myapp myapp=" + image)
    print("✅ Deployed version: " + version)
} else {
    print("⏭️  Skipping deployment (not on main branch)")
}
```

## Language Features

### Permissions (Deny-by-Default Security)

```glipt
# Explicit permissions required for dangerous operations
allow exec "git*"          # Allow running git commands
allow read "config/*"      # Allow reading config directory
allow write "logs/*"       # Allow writing to logs directory
allow env "DATABASE_URL"   # Allow accessing specific env var

# Without permissions, operations fail with clear errors
result = exec("rm -rf /")  # ❌ Permission denied: exec "rm"
```

### Error Handling

```glipt
on failure {
    print("Error: " + error["message"])
    print("Type: " + error["type"])

    # Cleanup, logging, rollback, etc.
    exec("cleanup.sh")
    exit(1)
}

# Any errors in subsequent code are caught
result = exec("might-fail-command")
data = parse_json(invalid_json)
```

### Parallel Execution

```glipt
# Run commands concurrently
results = parallel_exec([
    "curl https://api1.com/data",
    "curl https://api2.com/data",
    "curl https://api3.com/data"
])

for result in results {
    print(result["output"])
}
```

### Built-in JSON Support

```glipt
# Parse JSON
config = parse_json(read("config.json"))
print(config["database"]["host"])

# Serialize to JSON
data = {"name": "Alice", "age": 30, "active": true}
json_str = to_json(data)
write("output.json", json_str)
```

### Process Execution

```glipt
# Run commands safely (no shell injection via posix_spawn)
result = exec("git log --oneline -n 10")

print(result["output"])      # stdout with trailing newline stripped
print(result["stdout"])      # raw stdout
print(result["stderr"])      # stderr
print(result["exitCode"])    # exit code

assert(result["exitCode"] == 0, "Command failed")
```

## Language Syntax

### Variables

```glipt
x = 42                    # Implicit declaration
name = "Alice"            # Strings
active = true             # Booleans
nothing = nil             # Nil
```

### Functions

```glipt
fn greet(name) {
    return "Hello, " + name
}

# Closures
fn counter() {
    count = 0
    return fn() {
        count = count + 1
        return count
    }
}
```

### Control Flow

```glipt
# If/else
if x > 10 {
    print("big")
} else {
    print("small")
}

# While
while n < 100 {
    n = n * 2
}

# For-in
for item in [1, 2, 3, 4, 5] {
    print(item)
}
```

### Data Structures

```glipt
# Lists
nums = [1, 2, 3]
append(nums, 4)
print(nums[0])           # 1

# Maps
person = {"name": "Alice", "age": 30}
print(person["name"])    # Alice
print(person.age)        # 30 (dot notation)
```

## CLI Reference

```bash
glipt                          # Start REPL
glipt repl                     # Start REPL
glipt run <script>             # Run script (deny-by-default)
glipt run --allow-all <script> # Run with all permissions
glipt check <script>           # Syntax check only
glipt disasm <script>          # Show bytecode disassembly
glipt ast <script>             # Show AST (debug)
glipt tokens <script>          # Show tokens (debug)
glipt version                  # Show version
glipt help                     # Show help
```

## REPL

Start an interactive session:

```bash
$ ./glipt
Glipt 0.1.0 REPL (type 'exit' to quit)
>>> x = 42
>>> fn double(n) { return n * 2 }
>>> print(double(x))
84
>>> exit
```

The REPL has all permissions enabled and variables persist between lines.

## Build System

```bash
make              # Release build (-O2, strict warnings)
make debug        # Debug build (DEBUG_TRACE, stress GC, no optimization)
make test         # Build and run tests
make clean        # Remove build artifacts
```

## CI/CD

Every push and pull request is built and tested on Linux, macOS, and Windows via GitHub Actions.

**Cutting a release**: push a `v*` tag and GitHub Actions will build all three platform binaries and publish a GitHub Release automatically:

```bash
git tag v1.0.0
git push origin v1.0.0
```

## Testing

```bash
./glipt run --allow-all examples/milestone1.glipt   # Basic types (7 tests)
./glipt run --allow-all examples/milestone2.glipt   # Functions/closures (14 tests)
./glipt run --allow-all examples/milestone3.glipt   # Process/JSON/files (18 tests)
./glipt run --allow-all examples/full_test.glipt    # All features (37 assertions)
```

All tests exit with code 0 and produce expected output.

## Contributing

- C11 standard, 4-space indentation, no external dependencies
- Build must pass with `-Wall -Wextra -Wpedantic -Werror`

## License

MIT License - see LICENSE file
